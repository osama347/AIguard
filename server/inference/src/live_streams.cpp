#include "ai/live_streams.h"
#include "ai/job_queue.h"
#include "ai/logging.h"
#include "ai/perception.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>

using json = nlohmann::json;

namespace ai {

// ---------------------------------------------------------------- sources

namespace {

// "scheme://rest?k=v&k2=v2" -> rest, params
std::string splitQuery(const std::string& s, std::map<std::string, std::string>& params) {
    auto q = s.find('?');
    if (q == std::string::npos) return s;
    std::string query = s.substr(q + 1);
    std::size_t pos = 0;
    while (pos <= query.size()) {
        auto amp = query.find('&', pos);
        std::string kv = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        auto eq = kv.find('=');
        if (eq != std::string::npos) params[kv.substr(0, eq)] = kv.substr(eq + 1);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return s.substr(0, q);
}

int intOr(const std::map<std::string, std::string>& p, const std::string& k, int def) {
    auto it = p.find(k);
    if (it == p.end()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

// Converts whatever the decoder produced into BGR for OpenCV.
const char* kToBgr = "nvvidconv ! video/x-raw,format=BGRx ! videoconvert ! video/x-raw,format=BGR ! "
                     "appsink drop=true max-buffers=1 sync=false";

} // namespace

std::optional<OpenedSource> openSource(const std::string& source, cv::VideoCapture& cap, std::string& err) {
    std::map<std::string, std::string> p;

    if (startsWith(source, "csi://")) {
        std::string sensor = splitQuery(source.substr(6), p);
        if (!allDigits(sensor)) { err = "csi:// needs a sensor number, e.g. csi://0"; return std::nullopt; }
        const int w = intOr(p, "width", 1920), h = intOr(p, "height", 1080), fps = intOr(p, "fps", 30);
        const std::string pipe = "nvarguscamerasrc sensor-id=" + sensor + " ! video/x-raw(memory:NVMM),width=" +
                                 std::to_string(w) + ",height=" + std::to_string(h) + ",framerate=" +
                                 std::to_string(fps) + "/1 ! " + kToBgr;
        if (cap.open(pipe, cv::CAP_GSTREAMER)) return OpenedSource{"gstreamer", false};
        err = "cannot open CSI camera " + sensor + " (is it connected and nvargus-daemon running?)";
        return std::nullopt;
    }

    if (startsWith(source, "usb://") || startsWith(source, "/dev/video")) {
        std::string dev = splitQuery(startsWith(source, "usb://") ? source.substr(6) : source.substr(10), p);
        if (!allDigits(dev)) { err = "USB camera needs a device number, e.g. usb://0"; return std::nullopt; }
        if (!cap.open(std::stoi(dev), cv::CAP_V4L2)) {
            err = "cannot open /dev/video" + dev + " (is the camera plugged in?)";
            return std::nullopt;
        }
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));   // most USB cams: needed for HD fps
        if (p.count("width")) cap.set(cv::CAP_PROP_FRAME_WIDTH, intOr(p, "width", 1280));
        if (p.count("height")) cap.set(cv::CAP_PROP_FRAME_HEIGHT, intOr(p, "height", 720));
        cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
        return OpenedSource{"v4l2", false};
    }

    if (startsWith(source, "rtsp://") || startsWith(source, "rtsps://")) {
        // Hardware decoding via decodebin (picks nvv4l2decoder); TCP avoids
        // UDP packet loss artefacts; timeout turns a dead camera into an error.
        std::string loc = source;
        for (char& c : loc) if (c == '"') c = '\'';
        const std::string pipe = "rtspsrc location=\"" + loc + "\" latency=200 protocols=tcp timeout=5000000 ! "
                                 "decodebin ! " + kToBgr;
        if (cap.open(pipe, cv::CAP_GSTREAMER)) return OpenedSource{"gstreamer", false};
        if (cap.open(source, cv::CAP_FFMPEG)) return OpenedSource{"ffmpeg", false};
        err = "cannot connect to " + source;
        return std::nullopt;
    }

    if (startsWith(source, "http://") || startsWith(source, "https://")) {
        if (cap.open(source, cv::CAP_FFMPEG)) return OpenedSource{"ffmpeg", false};
        err = "cannot open " + source;
        return std::nullopt;
    }

    if (startsWith(source, "gst://")) {
        if (cap.open(source.substr(6), cv::CAP_GSTREAMER)) return OpenedSource{"gstreamer", false};
        err = "cannot start GStreamer pipeline";
        return std::nullopt;
    }

    std::string path = startsWith(source, "file://") ? source.substr(7) : source;
    path = splitQuery(path, p);
    if (!path.empty() && path[0] == '/' && cap.open(path)) return OpenedSource{"file", true};
    err = "cannot open source: " + source;
    return std::nullopt;
}

// ---------------------------------------------------------------- LiveStream

LiveStream::LiveStream(std::string id, std::string source, double sampleFps, Perception& perception,
                       const LiveConfig& cfg)
    : id_(std::move(id)), source_(std::move(source)), sampleFps_(sampleFps > 0 ? sampleFps : 5.0),
      perception_(perception), cfg_(cfg) {}

LiveStream::~LiveStream() { stop(); }

void LiveStream::start() {
    capture_ = std::thread([this] { captureLoop(); });
    analysis_ = std::thread([this] { analysisLoop(); });
}

void LiveStream::stop() {
    stop_ = true;
    cv_.notify_all();
    if (capture_.joinable()) capture_.join();
    if (analysis_.joinable()) analysis_.join();
}

bool LiveStream::sleepFor(std::chrono::milliseconds d) {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait_for(lk, d, [&] { return stop_.load(); });
    return !stop_;
}

void LiveStream::push(json event) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        event["seq"] = nextSeq_;
        events_.emplace_back(nextSeq_++, event.dump());
        while (events_.size() > static_cast<std::size_t>(std::max(10, cfg_.eventBuffer))) events_.pop_front();
    }
    cv_.notify_all();
}

void LiveStream::setState(const std::string& state, const std::string& message) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (state_ == state && message_ == message) return;
        state_ = state;
        message_ = message;
    }
    if (state == "live") LOG_INFO << "Stream " << id_ << " live";
    else LOG_WARN << "Stream " << id_ << " " << state << ": " << message;
    push({{"type", "status"}, {"state", state}, {"message", message}});
}

json LiveStream::info() {
    std::lock_guard<std::mutex> lk(mtx_);
    json lastFrame = nullptr;
    if (captured_ > 0)
        lastFrame = std::chrono::duration_cast<std::chrono::milliseconds>(latestAt_.time_since_epoch()).count();
    return {{"stream_id", id_}, {"source", source_}, {"sample_fps", sampleFps_},
            {"state", state_}, {"message", message_}, {"backend", backend_},
            {"width", width_}, {"height", height_},
            {"capture_fps", captureFps_}, {"analysis_fps", analysisFps_},
            {"frames_captured", captured_}, {"frames_analysed", analysed_},
            {"last_frame_ms", lastFrame}, {"next_seq", nextSeq_}};
}

std::string LiveStream::waitEvent(uint64_t& next, std::chrono::milliseconds timeout, bool& stopped) {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait_for(lk, timeout, [&] { return next < nextSeq_ || stop_; });
    stopped = stop_;
    if (next >= nextSeq_ || events_.empty()) return {};
    if (next < events_.front().first) next = events_.front().first;
    std::string ev = events_[next - events_.front().first].second;
    ++next;
    return ev;
}

std::optional<std::pair<uint64_t, std::string>> LiveStream::preview(uint64_t after, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lk(mtx_);
    previewWanted_ = std::chrono::steady_clock::now();
    cv_.wait_for(lk, timeout, [&] { return previewSeq_ > after || stop_; });
    if (previewSeq_ <= after || previewJpeg_.empty()) return std::nullopt;
    return std::make_pair(previewSeq_, previewJpeg_);
}

std::optional<std::string> LiveStream::frameImage(uint64_t frame) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& [f, jpeg] : images_)
        if (f == frame) return jpeg;
    return std::nullopt;
}

namespace {

std::string encodeJpeg(const cv::Mat& frame, int maxWidth, int quality) {
    cv::Mat out = frame;
    if (maxWidth > 0 && frame.cols > maxWidth) {
        cv::resize(frame, out, cv::Size(maxWidth, frame.rows * maxWidth / frame.cols), 0, 0, cv::INTER_AREA);
    }
    std::vector<uchar> buf;
    cv::imencode(".jpg", out, buf, {cv::IMWRITE_JPEG_QUALITY, quality});
    return std::string(buf.begin(), buf.end());
}

} // namespace

void LiveStream::captureLoop() {
    using clock = std::chrono::steady_clock;
    int backoffS = 1;
    while (!stop_) {
        setState(captured_ == 0 ? "connecting" : "reconnecting", "opening " + source_);
        cv::VideoCapture cap;
        std::string err;
        auto opened = openSource(source_, cap, err);
        if (!opened) {
            setState("reconnecting", err + "; retrying in " + std::to_string(backoffS) + " s");
            if (!sleepFor(std::chrono::seconds(backoffS))) break;
            backoffS = std::min(30, backoffS * 2);
            continue;
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            backend_ = opened->backend;
        }

        double fileFps = cap.get(cv::CAP_PROP_FPS);
        if (!(fileFps > 0 && fileFps < 240)) fileFps = 25.0;
        const auto fileFrame = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / fileFps));
        const auto previewEvery = std::chrono::duration_cast<clock::duration>(
            std::chrono::duration<double>(1.0 / std::max(1.0, cfg_.previewFps)));

        auto nextFileFrame = clock::now();
        auto nextPreview = clock::now();
        auto fpsWindow = clock::now();
        int fpsCount = 0;
        bool gotFrame = false;
        const std::string lost = "camera stopped sending frames";

        while (!stop_) {
            cv::Mat frame;   // fresh buffer each time: the analysis thread may still hold the previous one
            bool ok = cap.read(frame) && !frame.empty();
            if (!ok && opened->isFile && gotFrame) {   // test source: loop the clip
                cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                ok = cap.read(frame) && !frame.empty();
            }
            if (!ok) break;
            if (!gotFrame) {
                gotFrame = true;
                backoffS = 1;
                setState("live", opened->backend);
            }
            {
                std::lock_guard<std::mutex> lk(mtx_);
                latest_ = frame;
                ++captured_;
                latestAt_ = std::chrono::system_clock::now();
                width_ = frame.cols;
                height_ = frame.rows;
            }
            cv_.notify_all();

            const auto now = clock::now();
            ++fpsCount;
            if (now - fpsWindow >= std::chrono::seconds(2)) {
                std::lock_guard<std::mutex> lk(mtx_);
                captureFps_ = fpsCount / std::chrono::duration<double>(now - fpsWindow).count();
                fpsWindow = now;
                fpsCount = 0;
            }

            // Preview JPEGs only while someone watched in the last few seconds.
            bool wanted;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                wanted = now - previewWanted_ < std::chrono::seconds(5);
            }
            if (wanted && now >= nextPreview) {
                std::string jpeg = encodeJpeg(frame, cfg_.previewWidth, cfg_.jpegQuality);
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    previewJpeg_ = std::move(jpeg);
                    ++previewSeq_;
                }
                cv_.notify_all();
                nextPreview = now + previewEvery;
            }

            if (opened->isFile) {   // real-time pacing, like a camera
                nextFileFrame += fileFrame;
                if (nextFileFrame < clock::now()) nextFileFrame = clock::now();
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait_until(lk, nextFileFrame, [&] { return stop_.load(); });
            }
        }
        if (stop_) break;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            captureFps_ = 0;
        }
        setState("reconnecting", (gotFrame ? lost : "no frames from " + source_) +
                                     "; retrying in " + std::to_string(backoffS) + " s");
        if (!sleepFor(std::chrono::seconds(backoffS))) break;
        backoffS = std::min(30, backoffS * 2);
    }
}

void LiveStream::analysisLoop() {
    using clock = std::chrono::steady_clock;
    const auto interval = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / sampleFps_));
    uint64_t lastCaptured = 0;
    auto fpsWindow = clock::now();
    int fpsCount = 0;

    while (!stop_) {
        cv::Mat frame;
        std::chrono::system_clock::time_point at;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, std::chrono::seconds(1), [&] { return captured_ > lastCaptured || stop_; });
            if (stop_) break;
            if (captured_ == lastCaptured) continue;
            frame = latest_;
            at = latestAt_;
            lastCaptured = captured_;
        }
        const auto started = clock::now();

        FramePerception fp;
        try {
            fp = perception_.analyze(frame);
        } catch (const std::exception& e) {
            LOG_ERROR << "Stream " << id_ << " analysis failed: " << e.what();
            if (!sleepFor(std::chrono::seconds(1))) break;
            continue;
        }

        uint64_t index;
        const bool detections = !fp.faces.empty() || !fp.plates.empty();
        std::string jpeg;
        if (detections) jpeg = encodeJpeg(frame, 0, 85);   // full resolution: boxes use frame coordinates
        {
            std::lock_guard<std::mutex> lk(mtx_);
            index = ++analysed_;
            if (detections) {
                images_.emplace_back(index, std::move(jpeg));
                while (images_.size() > static_cast<std::size_t>(std::max(1, cfg_.frameImages))) images_.pop_front();
            }
        }

        const int64_t tsMs = std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count();
        json ev = frameEventJson(static_cast<int>(index), static_cast<double>(tsMs), fp);
        ev["ts"] = tsMs;
        ev["width"] = frame.cols;
        ev["height"] = frame.rows;
        ev["image"] = detections;
        push(std::move(ev));

        const auto now = clock::now();
        ++fpsCount;
        if (now - fpsWindow >= std::chrono::seconds(2)) {
            std::lock_guard<std::mutex> lk(mtx_);
            analysisFps_ = fpsCount / std::chrono::duration<double>(now - fpsWindow).count();
            fpsWindow = now;
            fpsCount = 0;
        }
        const auto wait = started + interval - clock::now();
        if (wait > clock::duration::zero()) {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, wait, [&] { return stop_.load(); });
        }
    }
}

// ---------------------------------------------------------------- StreamManager

StreamManager::~StreamManager() {
    std::map<std::string, std::shared_ptr<LiveStream>> all;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        all.swap(streams_);
    }
    for (auto& [id, s] : all) s->stop();
}

void StreamManager::retire(std::shared_ptr<LiveStream> s) {
    // A camera read can block for seconds (RTSP timeout); do not hold up the request.
    std::thread([s] { s->stop(); }).detach();
}

std::pair<std::shared_ptr<LiveStream>, bool> StreamManager::ensure(const std::string& id, const std::string& source,
                                                                   double sampleFps) {
    std::shared_ptr<LiveStream> old, created;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = streams_.find(id);
        if (it != streams_.end()) {
            if (it->second->source() == source && std::abs(it->second->sampleFps() - sampleFps) < 1e-6)
                return {it->second, false};
            old = it->second;
            streams_.erase(it);
        }
        if (streams_.size() >= static_cast<std::size_t>(cfg_.maxStreams))
            throw std::length_error("maximum number of live streams (" + std::to_string(cfg_.maxStreams) + ") reached");
        created = std::make_shared<LiveStream>(id, source, sampleFps, perception_, cfg_);
        streams_[id] = created;
    }
    if (old) retire(old);
    created->start();
    LOG_INFO << "Stream " << id << " started: " << source << " @ " << created->sampleFps() << " fps";
    return {created, true};
}

bool StreamManager::remove(const std::string& id) {
    std::shared_ptr<LiveStream> s;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = streams_.find(id);
        if (it == streams_.end()) return false;
        s = it->second;
        streams_.erase(it);
    }
    retire(s);
    LOG_INFO << "Stream " << id << " removed";
    return true;
}

std::shared_ptr<LiveStream> StreamManager::find(const std::string& id) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = streams_.find(id);
    return it == streams_.end() ? nullptr : it->second;
}

json StreamManager::list() {
    std::vector<std::shared_ptr<LiveStream>> all;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& [id, s] : streams_) all.push_back(s);
    }
    json arr = json::array();
    for (auto& s : all) arr.push_back(s->info());
    return arr;
}

std::size_t StreamManager::count() {
    std::lock_guard<std::mutex> lk(mtx_);
    return streams_.size();
}

json StreamManager::probe(const std::string& source) {
    cv::VideoCapture cap;
    std::string err;
    auto opened = openSource(source, cap, err);
    if (!opened) return {{"ok", false}, {"error", err}};
    cv::Mat frame;
    for (int i = 0; i < 30 && (frame.empty()); ++i) {   // cameras may deliver a few empty frames first
        if (!cap.read(frame)) break;
    }
    if (frame.empty()) return {{"ok", false}, {"error", "opened, but no frames received"}};
    double fps = cap.get(cv::CAP_PROP_FPS);
    return {{"ok", true}, {"backend", opened->backend}, {"width", frame.cols}, {"height", frame.rows},
            {"fps", fps > 0 && fps < 240 ? fps : 0.0}};
}

} // namespace ai
