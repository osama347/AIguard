#pragma once
#include "ai/config.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ai {

class Perception;
struct FramePerception;

// A "frame" event: {type, frame, t_ms, faces[{bbox, conf, embedding}],
// plates[{bbox, conf, text, text_conf}], timings}. Shared by jobs and streams.
nlohmann::json frameEventJson(int index, double tMs, const FramePerception& fp);

// ---------------------------------------------------------------------------
// A video analysis job and its event log.
//
// Events are appended in order and kept until the job is purged, so a stream
// consumer can (re)connect at any time and replay from any sequence number.
// Event types: start, frame, done, error, cancelled.
// ---------------------------------------------------------------------------
class Job {
public:
    Job(std::string id, std::string source, double sampleFps)
        : id_(std::move(id)), source_(std::move(source)), sampleFps_(sampleFps) {}

    const std::string& id() const { return id_; }
    const std::string& source() const { return source_; }
    double sampleFps() const { return sampleFps_; }

    void push(nlohmann::json event);       // appends; terminal types finish the job
    void requestCancel() { cancel_ = true; }
    bool cancelRequested() const { return cancel_; }

    // Wait until event `index` exists or the job has finished, up to `timeout`.
    // Returns the event (serialized JSON) or empty if none is available yet.
    std::string waitEvent(std::size_t index, std::chrono::milliseconds timeout, bool& finished);

    std::string status() const;
    bool finishedBefore(std::chrono::steady_clock::time_point t) const;

private:
    std::string id_, source_;
    double sampleFps_;
    std::atomic<bool> cancel_{false};

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::vector<std::string> events_;
    std::string status_ = "queued";
    bool finished_ = false;
    std::chrono::steady_clock::time_point finishedAt_;
};

// ---------------------------------------------------------------------------
// JobQueue: FIFO of video jobs processed by ONE worker thread (the GPU is the
// bottleneck; parallel jobs would only contend for it).
// ---------------------------------------------------------------------------
class JobQueue {
public:
    JobQueue(Perception& perception, VideoConfig cfg);
    ~JobQueue();

    // Throws std::invalid_argument if the id is already known.
    std::shared_ptr<Job> submit(const std::string& id, const std::string& source, double sampleFps);
    std::shared_ptr<Job> find(const std::string& id);
    bool cancel(const std::string& id);
    nlohmann::json stats();

private:
    void workerLoop();
    void run(Job& job);
    void runImage(Job& job, const std::string& path);
    static bool isImageFile(const std::string& path);
    void purgeFinished();

    Perception& perception_;
    VideoConfig cfg_;

    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<Job>> pending_;
    std::map<std::string, std::shared_ptr<Job>> jobs_;
    std::string running_;
    bool stop_ = false;
    std::thread worker_;
};

} // namespace ai
