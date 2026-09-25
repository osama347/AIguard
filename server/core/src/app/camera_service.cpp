#include "app/camera_service.h"
#include "app/access_recorder.h"
#include "app/errors.h"
#include "app/frame_annotator.h"
#include "app/serialization.h"
#include "common/log.h"
#include "domain/visit_tracker.h"
#include "infra/sqlite_db.h"
#include <algorithm>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace guard {

namespace {

using steady = std::chrono::steady_clock;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

// Epoch ms -> "YYYY-MM-DD HH:MM:SS" (UTC, as stored by SQLite); fmt selects parts.
std::string utc(int64_t ms, const char* fmt = "%Y-%m-%d %H:%M:%S") {
    std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}

bool knownSource(const std::string& s) {
    for (const char* p : {"csi://", "usb://", "/dev/video", "rtsp://", "rtsps://", "http://", "https://",
                          "file:///", "gst://"})
        if (s.rfind(p, 0) == 0) return true;
    return false;
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

} // namespace

// ======================================================================
// Supervisor: one camera
// ======================================================================

class CameraService::Supervisor {
public:
    Supervisor(Camera cam, CameraService& svc)
        : cam_(std::move(cam)), svc_(svc),
          tracker_(VisitConfig{static_cast<int64_t>(svc.cfg_.live.visitGapS * 1000),
                               static_cast<int64_t>(svc.cfg_.live.maxVisitS * 1000),
                               svc.cfg_.live.minVisitFrames}) {}
    ~Supervisor() { stop(); }

    void start() { thread_ = std::thread([this] { run(); }); }

    // `retire`: the camera is being disabled/removed (not just core shutting down),
    // so an open "offline" alert about it no longer applies.
    void stop(bool retire = false) {
        stop_ = true;
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
        {
            std::lock_guard<std::mutex> lk(visitMtx_);
            if (auto v = tracker_.flush()) finish(*v);
        }
        std::lock_guard<std::mutex> alk(alertMtx_);
        if (retire && offlineAlert_) {
            svc_.events_.resolve(*offlineAlert_, "system");
            if (auto a = svc_.events_.getAlert(*offlineAlert_)) svc_.bus_.publish("alert.updated", toJson(*a));
            offlineAlert_.reset();
        }
    }

    json status() {
        std::lock_guard<std::mutex> lk(mtx_);
        return {{"state", state_}, {"message", message_}, {"analysis_fps", analysisFps_},
                {"width", width_}, {"height", height_},
                {"last_frame_at", lastFrameMs_ ? json(utcIso(lastFrameMs_)) : json(nullptr)},
                {"visit_active", visitActive_}};
    }

    // Called every few seconds by the monitor thread.
    void tick() {
        const int64_t now = nowMs();
        {
            // Frames stopped arriving mid-visit (camera or inference died): close it.
            std::lock_guard<std::mutex> lk(visitMtx_);
            if (tracker_.active() && now - lastFrameTs_ > static_cast<int64_t>(svc_.cfg_.live.visitGapS * 1000) + 2000) {
                if (auto v = tracker_.flush()) finish(*v);
                setVisitActive(false);
            }
        }
        std::string state, message;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            state = state_;
            message = message_;
        }
        std::lock_guard<std::mutex> alk(alertMtx_);
        if (state == "live") {
            downSince_.reset();
            if (offlineAlert_) {
                svc_.events_.resolve(*offlineAlert_, "system");
                if (auto a = svc_.events_.getAlert(*offlineAlert_)) svc_.bus_.publish("alert.updated", toJson(*a));
                LOG(Info) << "Camera '" << cam_.name << "' back online";
                offlineAlert_.reset();
            }
            return;
        }
        if (state == "waiting" || state == "starting") {   // AI engine not ready: not the camera's fault
            downSince_.reset();
            return;
        }
        if (!downSince_) downSince_ = steady::now();
        const auto down = std::chrono::duration<double>(steady::now() - *downSince_).count();
        if (!offlineAlert_ && down >= svc_.cfg_.live.offlineAlertS) {
            const std::string why = message.substr(0, message.find("; retrying"));
            Alert a = svc_.events_.insertAlert("warning", "camera_offline",
                                               "Camera '" + cam_.name + "' is offline: " + why, std::nullopt);
            offlineAlert_ = a.id;
            svc_.bus_.publish("alert.created", toJson(a));
        }
    }

private:
    static std::string utcIso(int64_t ms) { return utc(ms, "%Y-%m-%dT%H:%M:%SZ"); }

    void setState(const std::string& state, const std::string& message) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (state_ == state && message_ == message) return;
            state_ = state;
            message_ = message;
            if (state != "live") analysisFps_ = 0;
        }
        svc_.bus_.publish("camera.status", {{"camera_id", cam_.id}, {"state", state}, {"message", message}},
                          "camera:" + std::to_string(cam_.id));
    }

    void setVisitActive(bool active) {
        std::lock_guard<std::mutex> lk(mtx_);
        visitActive_ = active;
    }

    bool waitFor(std::chrono::milliseconds d) {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait_for(lk, d, [&] { return stop_.load(); });
        return !stop_;
    }

    void run() {
        const std::string sid = streamId(cam_.id);
        while (!stop_) {
            const InferenceHealth h = svc_.inference_.health();
            if (h.status != "ok") {
                setState("waiting", h.reachable ? "AI engine is " + h.status + ": " + h.message : "AI engine is offline");
                if (!waitFor(std::chrono::seconds(3))) break;
                continue;
            }
            try {
                svc_.inference_.ensureStream(sid, cam_.source, cam_.sampleFps);
            } catch (const InferenceUnavailable&) {
                if (!waitFor(std::chrono::seconds(3))) break;
                continue;
            } catch (const InferenceError& e) {
                setState("error", e.what());
                if (!waitFor(std::chrono::seconds(10))) break;
                continue;
            }

            int64_t lastSeq = -1;
            for (;;) {
                InferenceClient::StreamEnd end;
                try {
                    end = svc_.inference_.streamEvents(sid, lastSeq, [&](const json& ev, int64_t seq) {
                        lastSeq = seq;
                        const std::string type = ev.value("type", "");
                        if (type == "frame") onFrame(ev);
                        else if (type == "status") onStatus(ev);
                        return !stop_.load();
                    }, &stop_);
                } catch (const InferenceUnavailable&) {
                    setState("waiting", "AI engine is offline");
                    waitFor(std::chrono::seconds(3));
                    break;   // re-check health and re-register
                }
                if (end == InferenceClient::StreamEnd::Stopped || stop_) return;
                if (end == InferenceClient::StreamEnd::NotFound) break;   // inference restarted: re-register
                if (!waitFor(std::chrono::seconds(1))) return;            // dropped: resume after lastSeq
            }
        }
    }

    void onStatus(const json& ev) {
        const std::string state = ev.value("state", "");
        setState(state == "live" ? "live" : state, ev.value("message", ""));
        if (state != "live") {   // camera lost: whatever was in view is over
            std::lock_guard<std::mutex> lk(visitMtx_);
            if (auto v = tracker_.flush()) finish(*v);
            setVisitActive(false);
        }
    }

    void onFrame(const json& ev) {
        if (fleetVersion_ != svc_.fleet_.version() || !fleet_.matcher) {
            fleetVersion_ = svc_.fleet_.version();
            fleet_ = svc_.fleet_.snapshot();
        }
        AnnotatedFrame a = annotateFrame(ev, fleet_);
        const FrameObservation& obs = a.observation;

        bool wasLive;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            wasLive = state_ == "live";
        }
        if (!wasLive) setState("live", "");
        {
            std::lock_guard<std::mutex> lk(mtx_);
            width_ = ev.value("width", 0);
            height_ = ev.value("height", 0);
            lastFrameMs_ = obs.tsMs;
            ++fpsCount_;
            const auto now = steady::now();
            if (now - fpsWindow_ >= std::chrono::seconds(2)) {
                analysisFps_ = fpsCount_ / std::chrono::duration<double>(now - fpsWindow_).count();
                fpsWindow_ = now;
                fpsCount_ = 0;
            }
        }

        bool active;
        {
            std::lock_guard<std::mutex> lk(visitMtx_);
            lastFrameTs_ = obs.tsMs;
            std::optional<Visit> ended = tracker_.observe(obs);
            if (ended) {
                std::string jpeg;
                if (ended->best) {
                    if (bestFrame_ == ended->best->frame) jpeg = std::move(bestJpeg_);
                    else jpeg = svc_.inference_.frameImage(streamId(cam_.id), ended->best->frame).value_or("");
                }
                bestJpeg_.clear();
                bestFrame_ = 0;
                finish(*ended, jpeg);
            }
            // Fetch the best frame of the running visit while inference still has it.
            const auto& cur = tracker_.current();
            if (cur && cur->best && cur->best->frame != bestFrame_ && steady::now() - lastFetch_ > std::chrono::milliseconds(400)) {
                if (auto img = svc_.inference_.frameImage(streamId(cam_.id), cur->best->frame)) {
                    bestJpeg_ = std::move(*img);
                    bestFrame_ = cur->best->frame;
                }
                lastFetch_ = steady::now();
            }
            active = tracker_.active();
        }
        setVisitActive(active);

        svc_.bus_.publishTransient("camera.frame",
            {{"camera_id", cam_.id}, {"frame", obs.frame}, {"ts", obs.tsMs},
             {"width", ev.value("width", 0)}, {"height", ev.value("height", 0)},
             {"faces", a.faces}, {"plates", a.plates}, {"visit_active", active}},
            "camera:" + std::to_string(cam_.id));
    }

    // visitMtx_ held.
    void finish(const Visit& v, const std::string& jpeg = "") {
        if (!v.meaningful()) return;   // only unreadable plates: nothing to identify
        const CoreConfig& cfg = svc_.cfg_;
        const PolicyConfig policy{cfg.policy.minFaceFrames, cfg.policy.minPlateReads};
        const Decision d = decideAccess(v.evidence, fleet_.assignments, policy);

        std::string plate = d.vehicle ? d.vehicle->text : "";
        if (plate.empty()) {
            int best = 0;
            for (const auto& [text, t] : v.evidence.plates())
                if (t.reads > best) { best = t.reads; plate = text; }
        }
        const std::string key = d.verdict + "|" + (d.driver ? std::to_string(d.driver->driverId) : "") + "|" + plate;
        if (lastEventId_ && key == lastKey_ &&
            v.startMs - lastEndMs_ <= static_cast<int64_t>(cfg.live.repeatSuppressS * 1000)) {
            // Same vehicle/person still around (e.g. waiting at the barrier): extend, don't repeat.
            svc_.events_.extendAccessEvent(lastEventId_, utc(v.endMs));
            lastEndMs_ = v.endMs;
            return;
        }

        AccessEvent base;
        base.cameraId = cam_.id;
        base.eventTime = utc(v.startMs);
        base.endedAt = utc(v.endMs);
        if (!jpeg.empty()) {
            const std::string rel = "snapshots/" + utc(v.startMs, "%Y-%m-%d") + "/cam" + std::to_string(cam_.id) +
                                    "-" + std::to_string(v.startMs) + ".jpg";
            std::error_code ec;
            fs::create_directories(fs::path(cfg.mediaDir() + "/" + rel).parent_path(), ec);
            std::ofstream out(cfg.mediaDir() + "/" + rel, std::ios::binary);
            out.write(jpeg.data(), static_cast<std::streamsize>(jpeg.size()));
            if (out) base.snapshot = rel;
        }
        json details{{"reason", d.reason}, {"frames", v.frames}, {"duration_ms", v.endMs - v.startMs}};
        if (v.best) details["overlay"] = json::parse(v.best->overlayJson, nullptr, false);
        base.detailsJson = details.dump();

        try {
            const RecordedAccess rec = recordAccess(svc_.events_, base, d, v.evidence);
            lastKey_ = key;
            lastEventId_ = rec.event.id;
            lastEndMs_ = v.endMs;
            svc_.bus_.publish("access.event", toJson(rec.event));
            if (rec.alert) svc_.bus_.publish("alert.created", toJson(*rec.alert));
            LOG(Info) << "Camera '" << cam_.name << "': " << d.verdict << " (" << d.reason << ")";
        } catch (const std::exception& e) {
            LOG(Error) << "Camera '" << cam_.name << "': cannot record event: " << e.what();
        }
    }

    Camera cam_;
    CameraService& svc_;
    std::atomic<bool> stop_{false};
    std::thread thread_;

    std::mutex mtx_;                  // status fields
    std::condition_variable cv_;
    std::string state_ = "starting", message_;
    double analysisFps_ = 0;
    int width_ = 0, height_ = 0;
    int64_t lastFrameMs_ = 0;
    bool visitActive_ = false;
    int fpsCount_ = 0;
    steady::time_point fpsWindow_ = steady::now();

    std::mutex visitMtx_;             // tracker and everything below
    VisitTracker tracker_;
    FleetSnapshot fleet_;
    uint64_t fleetVersion_ = 0;
    int64_t lastFrameTs_ = 0;
    std::string bestJpeg_;
    uint64_t bestFrame_ = 0;
    steady::time_point lastFetch_{};
    std::string lastKey_;
    int64_t lastEventId_ = 0, lastEndMs_ = 0;

    std::mutex alertMtx_;                           // offline alert bookkeeping
    std::optional<steady::time_point> downSince_;
    std::optional<int64_t> offlineAlert_;
};

// ======================================================================
// CameraService
// ======================================================================

CameraService::CameraService(CameraRepository& cameras, FleetService& fleet, EventRepository& events,
                             InferenceClient& inference, EventBus& bus, const CoreConfig& cfg)
    : cameras_(cameras), fleet_(fleet), events_(events), inference_(inference), bus_(bus), cfg_(cfg) {}

CameraService::~CameraService() { stop(); }

void CameraService::start() {
    std::error_code ec;
    fs::create_directories(cfg_.snapshotsDir(), ec);
    int started = 0;
    for (const auto& c : cameras_.list()) {
        if (c.enabled) { startSupervisor(c); ++started; }
        else inference_.removeStream(streamId(c.id));   // in case it was left running
    }
    if (started) LOG(Info) << "Watching " << started << " camera(s)";
    monitor_ = std::thread([this] { monitorLoop(); });
}

void CameraService::stop() {
    std::map<int64_t, std::shared_ptr<Supervisor>> all;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (stop_) return;
        stop_ = true;
        all.swap(supervisors_);
    }
    cv_.notify_all();
    if (monitor_.joinable()) monitor_.join();
    for (auto& [id, s] : all) s->stop();
}

void CameraService::startSupervisor(const Camera& c) {
    auto s = std::make_shared<Supervisor>(c, *this);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        supervisors_[c.id] = s;
    }
    s->start();
}

void CameraService::stopSupervisor(int64_t id) {
    std::shared_ptr<Supervisor> s;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = supervisors_.find(id);
        if (it == supervisors_.end()) return;
        s = it->second;
        supervisors_.erase(it);
    }
    s->stop(true);
}

Camera CameraService::get(int64_t id) {
    auto c = cameras_.get(id);
    if (!c) throw NotFoundError("camera " + std::to_string(id) + " not found");
    return *c;
}

void CameraService::validate(Camera& c) {
    c.name = trim(c.name);
    c.source = trim(c.source);
    if (c.name.empty() || c.name.size() > 80) throw ValidationError("name must be 1-80 characters");
    if (c.source.empty() || c.source.size() > 1000) throw ValidationError("source is required");
    if (!knownSource(c.source))
        throw ValidationError("source must start with csi://, usb://, /dev/video, rtsp://, http(s)://, "
                              "file:/// or gst://");
    if (!(c.sampleFps >= 0.5 && c.sampleFps <= 15)) throw ValidationError("sample_fps must be between 0.5 and 15");
}

Camera CameraService::create(Camera c, const std::string& actor) {
    validate(c);
    int64_t id;
    try {
        id = cameras_.create(c);
    } catch (const DbConstraintError&) {
        throw ConflictError("a camera named '" + c.name + "' already exists");
    }
    events_.audit(actor, "create", "camera", std::to_string(id), c.name + " " + c.source);
    Camera created = get(id);
    if (created.enabled) startSupervisor(created);
    return created;
}

Camera CameraService::update(Camera c, const std::string& actor) {
    validate(c);
    try {
        if (!cameras_.update(c)) throw NotFoundError("camera " + std::to_string(c.id) + " not found");
    } catch (const DbConstraintError&) {
        throw ConflictError("a camera named '" + c.name + "' already exists");
    }
    events_.audit(actor, "update", "camera", std::to_string(c.id),
                  c.name + " " + c.source + (c.enabled ? "" : " (disabled)"));
    stopSupervisor(c.id);
    Camera updated = get(c.id);
    if (updated.enabled) startSupervisor(updated);   // re-registers; inference restarts the stream if needed
    else inference_.removeStream(streamId(c.id));
    return updated;
}

void CameraService::remove(int64_t id, const std::string& actor) {
    get(id);
    stopSupervisor(id);
    inference_.removeStream(streamId(id));
    cameras_.remove(id);
    events_.audit(actor, "delete", "camera", std::to_string(id));
}

json CameraService::status(int64_t id) {
    std::shared_ptr<Supervisor> s;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = supervisors_.find(id);
        if (it != supervisors_.end()) s = it->second;
    }
    if (!s) return {{"state", "disabled"}, {"message", ""}, {"analysis_fps", 0}, {"width", 0}, {"height", 0},
                    {"last_frame_at", nullptr}, {"visit_active", false}};
    return s->status();
}

json CameraService::probe(const std::string& source) {
    Camera c;
    c.name = "probe";
    c.source = source;
    validate(c);
    try {
        return inference_.probe(c.source);
    } catch (const InferenceUnavailable&) {
        throw UnavailableError("the AI engine is not running");
    }
}

std::optional<std::pair<uint64_t, std::string>> CameraService::preview(int64_t id, uint64_t after) {
    Camera c = get(id);
    if (!c.enabled) throw ConflictError("camera is disabled");
    try {
        return inference_.preview(streamId(id), after);
    } catch (const InferenceUnavailable&) {
        return std::nullopt;
    }
}

std::string CameraService::snapshotFile(const AccessEvent& e) const {
    if (e.snapshot.empty() || e.snapshot.find("..") != std::string::npos) return "";
    return cfg_.mediaDir() + "/" + e.snapshot;
}

void CameraService::monitorLoop() {
    auto lastRetention = steady::time_point{};
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, std::chrono::seconds(2), [&] { return stop_; });
            if (stop_) return;
        }
        std::vector<std::shared_ptr<Supervisor>> all;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto& [id, s] : supervisors_) all.push_back(s);
        }
        for (auto& s : all) {
            try { s->tick(); }
            catch (const std::exception& e) { LOG(Error) << "Camera monitor: " << e.what(); }
        }
        if (steady::now() - lastRetention > std::chrono::hours(1)) {
            lastRetention = steady::now();
            try { enforceRetention(); }
            catch (const std::exception& e) { LOG(Error) << "Snapshot retention: " << e.what(); }
        }
    }
}

// Snapshots are stored per UTC day (media/snapshots/YYYY-MM-DD/). Days older
// than the retention period go first; if the disk is still low, the oldest
// remaining days follow. The access log rows themselves are kept.
void CameraService::enforceRetention() {
    const std::string root = cfg_.snapshotsDir();
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    std::vector<std::string> days;
    for (const auto& e : fs::directory_iterator(root, ec))
        if (e.is_directory()) days.push_back(e.path().filename().string());
    std::sort(days.begin(), days.end());

    auto dropDay = [&](const std::string& day, const char* why) {
        std::error_code rec;
        const auto n = fs::remove_all(root + "/" + day, rec);
        const int rows = events_.clearSnapshots("snapshots/" + day + "/");
        LOG(Info) << "Deleted snapshots of " << day << " (" << why << "): " << n << " files, " << rows << " events";
    };

    const std::string cutoff = utc(nowMs() - static_cast<int64_t>(cfg_.live.snapshotRetentionDays) * 86400000LL,
                                   "%Y-%m-%d");
    std::size_t i = 0;
    for (; i < days.size() && days[i] < cutoff; ++i) dropDay(days[i], "older than retention");

    const std::string today = utc(nowMs(), "%Y-%m-%d");
    auto lowDisk = [&] {
        auto sp = fs::space(cfg_.dataDir(), ec);
        return !ec && sp.available < static_cast<uintmax_t>(cfg_.live.minFreeDiskMb) * 1024 * 1024;
    };
    for (; i < days.size() && days[i] < today && lowDisk(); ++i) dropDay(days[i], "low disk space");
}

} // namespace guard
