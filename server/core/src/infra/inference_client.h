#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace guard {

// Connection-level failure: inference is not running / not reachable.
class InferenceUnavailable : public std::runtime_error {
public:
    explicit InferenceUnavailable(const std::string& m) : std::runtime_error(m) {}
};

// The service answered with an error status.
class InferenceError : public std::runtime_error {
public:
    InferenceError(int status, const std::string& m) : std::runtime_error(m), status_(status) {}
    int status() const { return status_; }
private:
    int status_;
};

struct InferenceHealth {
    bool reachable = false;
    std::string status = "offline";   // offline | loading | ok | error
    std::string message;
};

struct EmbedResult {
    std::optional<std::vector<float>> embedding;   // nullopt: no face / bad image
    std::string error;
};

// ---------------------------------------------------------------------------
// HTTP client for guard-inference's internal API (see inference/src/main.cpp).
// Stateless and cheap to call from any thread.
// ---------------------------------------------------------------------------
class InferenceClient {
public:
    InferenceClient(std::string baseUrl, std::string secret);

    InferenceHealth health() const;
    nlohmann::json models() const;

    void submitJob(const std::string& jobId, const std::string& source, double sampleFps) const;
    void cancelJob(const std::string& jobId) const;

    enum class StreamEnd { Finished, Disconnected, NotFound, Stopped };
    // Blocks while events arrive. onEvent returns false to stop early.
    // Resumes after sequence number `lastSeq` (-1 = from the start).
    StreamEnd stream(const std::string& jobId, int64_t lastSeq,
                     const std::function<bool(const nlohmann::json& event, int64_t seq)>& onEvent) const;

    // One result per image, same order. `images` are raw encoded bytes.
    std::vector<EmbedResult> embed(const std::vector<std::string>& images) const;

    // ---- live camera streams (inference keeps them running on its own)
    nlohmann::json ensureStream(const std::string& streamId, const std::string& source, double sampleFps) const;
    void removeStream(const std::string& streamId) const;
    // Events: status, frame. Never "finishes"; ends on disconnect or NotFound,
    // or with Stopped within ~2 s of `abort` becoming true.
    StreamEnd streamEvents(const std::string& streamId, int64_t lastSeq,
                           const std::function<bool(const nlohmann::json& event, int64_t seq)>& onEvent,
                           const std::atomic<bool>* abort = nullptr) const;
    // Newest preview JPEG newer than `after` (long-poll, ~3 s): {seq, jpeg}.
    std::optional<std::pair<uint64_t, std::string>> preview(const std::string& streamId, uint64_t after) const;
    std::optional<std::string> frameImage(const std::string& streamId, uint64_t frame) const;
    nlohmann::json probe(const std::string& source) const;

private:
    StreamEnd readSse(const std::string& path, int64_t lastSeq,
                      const std::function<bool(const nlohmann::json& event, int64_t seq)>& onEvent,
                      const std::atomic<bool>* abort = nullptr) const;

    std::string baseUrl_, secret_;
};

} // namespace guard
