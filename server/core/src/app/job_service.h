#pragma once
#include "app/event_bus.h"
#include "app/fleet_service.h"
#include "common/config.h"
#include "infra/inference_client.h"
#include "infra/repositories.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace guard {

// ---------------------------------------------------------------------------
// JobService: persisted analysis jobs and the single runner that feeds them
// to guard-inference one at a time.
//
// Per job: submit -> consume the perception stream (resuming after drops) ->
// match faces / plates per frame -> publish live frames -> decide -> store
// result, access event, alerts. If inference restarts mid-job the job is
// resubmitted, up to inference.max_attempts.
// ---------------------------------------------------------------------------
class JobService {
public:
    JobService(JobRepository& jobs, FleetService& fleet, EventRepository& events,
               InferenceClient& inference, EventBus& bus, const CoreConfig& cfg);
    ~JobService();

    void start();
    void stop();

    std::string newJobId() const;
    std::string mediaPathFor(const std::string& jobId, const std::string& originalName) const;
    std::string framesPath(const std::string& jobId) const;

    // Registers an uploaded file (already written to mediaPathFor()) as a job.
    Job createUpload(const std::string& jobId, const std::string& path, const std::string& originalName,
                     int64_t sizeBytes, const std::string& actor);

    Job get(const std::string& id);
    int queuePosition(const Job& job);
    std::vector<Job> list(int limit, int offset) { return jobs_.list(limit, offset); }
    void cancel(const std::string& id, const std::string& actor);
    void remove(const std::string& id, const std::string& actor);

    nlohmann::json runnerStatus();

private:
    void runLoop();
    void process(Job job);
    void finishSuccess(const Job& job, const std::string& resultJson, const nlohmann::json& frames,
                       const Decision& decision, const Evidence& evidence);
    void waitFor(std::chrono::milliseconds d);

    JobRepository& jobs_;
    FleetService& fleet_;
    EventRepository& events_;
    InferenceClient& inference_;
    EventBus& bus_;
    const CoreConfig& cfg_;

    std::thread worker_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::string current_;
    std::atomic<bool> cancelCurrent_{false};
};

} // namespace guard
