#include "ai/job_queue.h"
#include "ai/base64.h"
#include "ai/logging.h"
#include "ai/perception.h"
#include "ai/timing.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <cmath>
#include <stdexcept>

using json = nlohmann::json;

namespace ai {

// ---------------------------------------------------------------- Job

void Job::push(json event) {
    const std::string type = event.value("type", "");
    {
        std::lock_guard<std::mutex> lk(mtx_);
        event["seq"] = events_.size();
        events_.push_back(event.dump());
        if (type == "start") status_ = "running";
        if (type == "done" || type == "error" || type == "cancelled") {
            status_ = type;
            finished_ = true;
            finishedAt_ = std::chrono::steady_clock::now();
        }
    }
    cv_.notify_all();
}

std::string Job::waitEvent(std::size_t index, std::chrono::milliseconds timeout, bool& finished) {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait_for(lk, timeout, [&] { return index < events_.size() || finished_; });
    finished = finished_ && index >= events_.size();
    return index < events_.size() ? events_[index] : std::string();
}

std::string Job::status() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return status_;
}

bool Job::finishedBefore(std::chrono::steady_clock::time_point t) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return finished_ && finishedAt_ < t;
}

// ---------------------------------------------------------------- JobQueue

JobQueue::JobQueue(Perception& perception, VideoConfig cfg)
    : perception_(perception), cfg_(cfg), worker_([this] { workerLoop(); }) {}

JobQueue::~JobQueue() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_ = true;
        for (auto& [id, job] : jobs_) job->requestCancel();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::shared_ptr<Job> JobQueue::submit(const std::string& id, const std::string& source, double sampleFps) {
    purgeFinished();
    auto job = std::make_shared<Job>(id, source, sampleFps > 0 ? sampleFps : cfg_.sampleFps);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (jobs_.count(id)) throw std::invalid_argument("job already exists: " + id);
        jobs_[id] = job;
        pending_.push_back(job);
    }
    cv_.notify_all();
    return job;
}

std::shared_ptr<Job> JobQueue::find(const std::string& id) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = jobs_.find(id);
    return it == jobs_.end() ? nullptr : it->second;
}

bool JobQueue::cancel(const std::string& id) {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) return false;
        job = it->second;
        // Not started yet: drop it from the queue and finish it right away.
        for (auto p = pending_.begin(); p != pending_.end(); ++p) {
            if ((*p)->id() == id) {
                pending_.erase(p);
                job->push({{"type", "cancelled"}});
                return true;
            }
        }
    }
    job->requestCancel();
    return true;
}

json JobQueue::stats() {
    std::lock_guard<std::mutex> lk(mtx_);
    json pending = json::array();
    for (const auto& j : pending_) pending.push_back(j->id());
    return {{"running", running_.empty() ? json(nullptr) : json(running_)},
            {"pending", pending},
            {"known", jobs_.size()}};
}

void JobQueue::purgeFinished() {
    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(cfg_.retainFinishedS);
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (it->second->finishedBefore(cutoff)) it = jobs_.erase(it);
        else ++it;
    }
}

void JobQueue::workerLoop() {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [&] { return stop_ || !pending_.empty(); });
            if (stop_) return;
            job = pending_.front();
            pending_.pop_front();
            running_ = job->id();
        }
        try {
            run(*job);
        } catch (const std::exception& e) {
            LOG_ERROR << "Job " << job->id() << " failed: " << e.what();
            job->push({{"type", "error"}, {"detail", e.what()}});
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            running_.clear();
        }
    }
}

namespace {
json bboxJson(const BBox& b) { return json::array({b.x, b.y, b.w, b.h}); }
} // namespace

json frameEventJson(int index, double tMs, const FramePerception& fp) {
    json faces = json::array();
    for (const auto& f : fp.faces) {
        json fj{{"bbox", bboxJson(f.bbox)}, {"conf", f.confidence}};
        if (!f.embedding.empty()) fj["embedding"] = embedToBase64(f.embedding);
        faces.push_back(std::move(fj));
    }
    json plates = json::array();
    for (const auto& p : fp.plates) {
        plates.push_back({{"bbox", bboxJson(p.bbox)}, {"conf", p.confidence},
                          {"text", p.text}, {"text_conf", p.textConfidence}, {"char_conf", p.charConfidences}});
    }
    return {{"type", "frame"}, {"frame", index}, {"t_ms", tMs},
            {"faces", std::move(faces)}, {"plates", std::move(plates)}, {"timings", fp.timingsMs}};
}

bool JobQueue::isImageFile(const std::string& path) {
    std::string ext = path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.'));
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" || ext == ".webp";
}

// A still picture is a one-frame clip: same models, same events.
void JobQueue::runImage(Job& job, const std::string& path) {
    cv::Mat frame = cv::imread(path, cv::IMREAD_COLOR);
    if (frame.empty()) throw std::runtime_error("cannot read image: " + job.source());

    job.push({{"type", "start"}, {"total_frames", 1}, {"fps", 1.0},
              {"width", frame.cols}, {"height", frame.rows}, {"sample_every", 1}});
    LOG_INFO << "Job " << job.id() << " started: " << job.source() << " (image " << frame.cols << "x" << frame.rows << ")";

    Stopwatch sw;
    job.push(frameEventJson(0, 0.0, perception_.analyze(frame)));
    job.push({{"type", "done"}, {"frames_read", 1}, {"frames_analysed", 1}, {"elapsed_ms", sw.elapsedMs()}});
}

void JobQueue::run(Job& job) {
    if (job.cancelRequested()) { job.push({{"type", "cancelled"}}); return; }

    std::string source = job.source();
    if (source.rfind("file://", 0) == 0) source = source.substr(7);

    if (isImageFile(source)) { runImage(job, source); return; }

    cv::VideoCapture cap(source);
    if (!cap.isOpened()) throw std::runtime_error("cannot open video source: " + job.source());

    double fps = cap.get(cv::CAP_PROP_FPS);
    if (!(fps > 0 && fps < 240)) fps = 25.0;   // streams often report 0
    const int total = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    const int every = std::max(1, static_cast<int>(std::lround(fps / job.sampleFps())));

    job.push({{"type", "start"}, {"total_frames", std::max(0, total)}, {"fps", fps},
              {"width", static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH))},
              {"height", static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT))},
              {"sample_every", every}});
    LOG_INFO << "Job " << job.id() << " started: " << job.source() << " (" << total
             << " frames @ " << fps << " fps, every " << every << ")";

    Stopwatch sw;
    cv::Mat frame;
    int index = 0, analysed = 0;
    while (cap.read(frame)) {
        if (job.cancelRequested()) {
            job.push({{"type", "cancelled"}, {"frames_analysed", analysed}});
            return;
        }
        if (index % every == 0 && !frame.empty()) {
            job.push(frameEventJson(index, index * 1000.0 / fps, perception_.analyze(frame)));
            ++analysed;
        }
        ++index;
    }
    job.push({{"type", "done"}, {"frames_read", index}, {"frames_analysed", analysed},
              {"elapsed_ms", sw.elapsedMs()}});
    LOG_INFO << "Job " << job.id() << " done: " << analysed << " frames in "
             << static_cast<int>(sw.elapsedMs()) << " ms";
}

} // namespace ai
