#include "app/job_service.h"
#include "app/access_recorder.h"
#include "app/errors.h"
#include "app/serialization.h"
#include "common/base64.h"
#include "common/log.h"
#include "infra/crypto.h"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace guard {

namespace {

std::string safeExtension(const std::string& name) {
    std::string ext = fs::path(name).extension().string();
    if (ext.size() > 6) return ".mp4";
    for (char c : ext.substr(ext.empty() ? 0 : 1))
        if (!std::isalnum(static_cast<unsigned char>(c))) return ".mp4";
    return ext.empty() ? ".mp4" : ext;
}

} // namespace

JobService::JobService(JobRepository& jobs, FleetService& fleet, EventRepository& events,
                       InferenceClient& inference, EventBus& bus, const CoreConfig& cfg)
    : jobs_(jobs), fleet_(fleet), events_(events), inference_(inference), bus_(bus), cfg_(cfg) {}

JobService::~JobService() { stop(); }

void JobService::start() {
    fs::create_directories(cfg_.mediaDir());
    if (int n = jobs_.requeueInterrupted()) LOG(Info) << "Requeued " << n << " interrupted job(s)";
    worker_ = std::thread([this] { runLoop(); });
}

void JobService::stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (stop_) return;
        stop_ = true;
    }
    cancelCurrent_ = true;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::string JobService::newJobId() const { return crypto::randomHex(8); }

std::string JobService::mediaPathFor(const std::string& jobId, const std::string& originalName) const {
    return cfg_.mediaDir() + "/" + jobId + safeExtension(originalName);
}

std::string JobService::framesPath(const std::string& jobId) const {
    return cfg_.mediaDir() + "/" + jobId + ".frames.json";
}

Job JobService::createUpload(const std::string& jobId, const std::string& path, const std::string& originalName,
                             int64_t sizeBytes, const std::string& actor) {
    Job j;
    j.id = jobId;
    j.source = path;
    j.originalName = originalName.substr(0, 200);
    j.sizeBytes = sizeBytes;
    j.createdBy = actor;
    jobs_.create(j);
    events_.audit(actor, "create", "job", jobId, j.originalName);
    Job created = get(jobId);
    bus_.publish("job.queued", toJson(created, queuePosition(created)), jobId);
    cv_.notify_all();
    return created;
}

Job JobService::get(const std::string& id) {
    auto j = jobs_.get(id);
    if (!j) throw NotFoundError("job " + id + " not found");
    return *j;
}

int JobService::queuePosition(const Job& job) {
    return job.status == "queued" ? jobs_.queuedAhead(job.id) : -1;
}

void JobService::cancel(const std::string& id, const std::string& actor) {
    Job j = get(id);
    if (j.status == "queued") {
        jobs_.cancel(id);
        bus_.publish("job.cancelled", toJson(get(id)), id);
    } else if (j.status == "running") {
        std::lock_guard<std::mutex> lk(mtx_);
        if (current_ == id) cancelCurrent_ = true;
    } else {
        throw ConflictError("job is already " + j.status);
    }
    events_.audit(actor, "cancel", "job", id);
}

void JobService::remove(const std::string& id, const std::string& actor) {
    Job j = get(id);
    if (j.status == "queued" || j.status == "running") throw ConflictError("cancel the job before deleting it");
    std::error_code ec;
    fs::remove(j.source, ec);
    fs::remove(framesPath(id), ec);
    jobs_.remove(id);
    events_.audit(actor, "delete", "job", id);
}

json JobService::runnerStatus() {
    std::lock_guard<std::mutex> lk(mtx_);
    return {{"current_job", current_.empty() ? json(nullptr) : json(current_)}};
}

void JobService::waitFor(std::chrono::milliseconds d) {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait_for(lk, d, [&] { return stop_; });
}

void JobService::runLoop() {
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (stop_) return;
        }
        auto next = jobs_.nextQueued();
        if (!next) { waitFor(std::chrono::seconds(5)); continue; }
        if (inference_.health().status != "ok") { waitFor(std::chrono::seconds(3)); continue; }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            current_ = next->id;
            cancelCurrent_ = false;
        }
        try {
            process(*next);
        } catch (const std::exception& e) {
            LOG(Error) << "Job " << next->id << " failed: " << e.what();
            jobs_.fail(next->id, e.what());
            bus_.publish("job.failed", toJson(get(next->id)), next->id);
        }
        std::lock_guard<std::mutex> lk(mtx_);
        current_.clear();
    }
}

void JobService::process(Job job) {
    if (!fs::exists(job.source)) throw std::runtime_error("video file is missing: " + job.source);

    const int attempt = job.attempts + 1;
    const std::string inferenceId = attempt == 1 ? job.id : job.id + "-a" + std::to_string(attempt);
    try {
        inference_.submitJob(inferenceId, "file://" + fs::absolute(job.source).string(), cfg_.inference.sampleFps);
    } catch (const InferenceUnavailable&) {
        waitFor(std::chrono::seconds(3));   // stays queued; retried when inference is back
        return;
    } catch (const InferenceError& e) {
        if (e.status() == 503) { waitFor(std::chrono::seconds(3)); return; }
        throw;
    }
    jobs_.markRunning(job.id);
    job = get(job.id);
    LOG(Info) << "Job " << job.id << " running (attempt " << attempt << ")";

    // Fleet snapshot for this job: frame processing does no database lookups.
    const FleetSnapshot fleet = fleet_.snapshot();

    Evidence evidence;
    json frames = json::array();
    json video = json::object();
    int totalFrames = 0;
    int64_t lastSeq = -1;
    std::string terminal;
    json terminalData;
    auto lastProgressWrite = std::chrono::steady_clock::now();

    auto onEvent = [&](const json& ev, int64_t seq) -> bool {
        if (cancelCurrent_) return false;
        lastSeq = seq;
        const std::string type = ev.value("type", "");
        if (type == "start") {
            totalFrames = ev.value("total_frames", 0);
            video = {{"width", ev.value("width", 0)}, {"height", ev.value("height", 0)},
                     {"fps", ev.value("fps", 0.0)}, {"total_frames", totalFrames}};
            bus_.publish("job.started", {{"job", toJson(job)}, {"video", video}}, job.id);
        } else if (type == "frame") {
            AnnotatedFrame a = annotateFrame(ev, fleet);
            evidence.addFrame();
            for (const auto& f : a.observation.faces) evidence.addFace(f);
            for (const auto& p : a.observation.plates) evidence.addPlate(p.text, p.vehicleId, p.vehicleStatus);
            json& faces = a.faces;
            json& plates = a.plates;
            const int frameIndex = ev.value("frame", 0);
            const double progress = totalFrames > 0 ? std::min(1.0, (frameIndex + 1.0) / totalFrames) : 0.0;
            json frame{{"frame", frameIndex}, {"t_ms", ev.value("t_ms", 0.0)}, {"faces", faces}, {"plates", plates}};
            json live = frame;
            live["job_id"] = job.id;
            live["progress"] = progress;
            bus_.publish("job.frame", live, job.id);
            frames.push_back(std::move(frame));

            auto now = std::chrono::steady_clock::now();
            if (now - lastProgressWrite > std::chrono::seconds(1)) {
                jobs_.setProgress(job.id, progress);
                lastProgressWrite = now;
            }
        } else if (type == "done" || type == "error" || type == "cancelled") {
            terminal = type;
            terminalData = ev;
        }
        return true;
    };

    for (;;) {
        InferenceClient::StreamEnd end;
        try {
            end = inference_.stream(inferenceId, lastSeq, onEvent);
        } catch (const InferenceUnavailable&) {
            LOG(Warn) << "Inference unreachable during job " << job.id << "; waiting";
            waitFor(std::chrono::seconds(3));
            if (cancelCurrent_) end = InferenceClient::StreamEnd::Stopped;
            else continue;
        }

        if (end == InferenceClient::StreamEnd::Stopped) {
            inference_.cancelJob(inferenceId);
            jobs_.cancel(job.id);
            bus_.publish("job.cancelled", toJson(get(job.id)), job.id);
            LOG(Info) << "Job " << job.id << " cancelled";
            return;
        }
        if (end == InferenceClient::StreamEnd::NotFound) {
            // Inference restarted and lost the job.
            if (attempt < cfg_.inference.maxAttempts) {
                LOG(Warn) << "Inference lost job " << job.id << "; requeueing";
                jobs_.requeue(job.id);
                return;
            }
            throw std::runtime_error("inference lost the job " + std::to_string(attempt) + " times");
        }
        if (end == InferenceClient::StreamEnd::Disconnected && terminal.empty()) {
            waitFor(std::chrono::seconds(1));
            continue;   // resume after lastSeq
        }
        break;
    }

    if (terminal == "error") throw std::runtime_error(terminalData.value("detail", "analysis failed"));
    if (terminal == "cancelled") {
        jobs_.cancel(job.id);
        bus_.publish("job.cancelled", toJson(get(job.id)), job.id);
        return;
    }

    const PolicyConfig policy{cfg_.policy.minFaceFrames, cfg_.policy.minPlateReads};
    const Decision decision = decideAccess(evidence, fleet.assignments, policy);
    json result = toJson(decision, evidence);
    result["video"] = video;
    result["processing_ms"] = terminalData.value("elapsed_ms", 0.0);
    json framesDoc{{"video", video}, {"frames", frames}};
    finishSuccess(job, result.dump(), framesDoc, decision, evidence);
}

void JobService::finishSuccess(const Job& job, const std::string& resultJson, const json& framesDoc,
                               const Decision& decision, const Evidence& evidence) {
    std::ofstream(framesPath(job.id)) << framesDoc.dump();
    jobs_.complete(job.id, decision.verdict, resultJson);

    AccessEvent base;
    base.jobId = job.id;
    base.detailsJson = json{{"reason", decision.reason}}.dump();
    const RecordedAccess rec = recordAccess(events_, base, decision, evidence);

    bus_.publish("job.completed", toJson(get(job.id)), job.id);
    bus_.publish("access.event", toJson(rec.event));
    if (rec.alert) bus_.publish("alert.created", toJson(*rec.alert));
    LOG(Info) << "Job " << job.id << " completed: " << decision.verdict << " (" << decision.reason << ")";
}

} // namespace guard
