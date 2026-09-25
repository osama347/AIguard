#pragma once
#include <nlohmann/json.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace guard {

struct BusEvent {
    uint64_t id = 0;
    std::string type;       // job.*, camera.*, access.event, alert.*
    std::string topic;      // job id, "camera:<id>", or empty
    std::string json;       // serialized {"type", "data"}
};

// ---------------------------------------------------------------------------
// EventBus: in-process publish/subscribe with a bounded replay buffer.
//
// Ids increase monotonically (seeded from the wall clock so they keep
// increasing across restarts), letting SSE clients resume with Last-Event-ID.
// Durable outcomes (job results, access events, alerts) live in the database;
// the bus only carries live updates.
// ---------------------------------------------------------------------------
class EventBus {
public:
    explicit EventBus(std::size_t capacity = 5000);

    uint64_t publish(const std::string& type, const nlohmann::json& data, const std::string& topic = "");

    // High-rate live updates (camera frames): kept in a small separate buffer
    // so they never push durable-outcome notifications out of the replay window.
    uint64_t publishTransient(const std::string& type, const nlohmann::json& data, const std::string& topic);

    // Events with id > afterId (optionally only for one topic), in id order.
    // Blocks up to `timeout` when none are available. Returns empty on timeout.
    std::vector<BusEvent> waitAfter(uint64_t afterId, std::chrono::milliseconds timeout,
                                    const std::string& topicFilter);

    uint64_t lastId();

private:
    std::size_t capacity_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<BusEvent> events_;
    std::deque<BusEvent> transient_;
    uint64_t nextId_;
};

} // namespace guard
