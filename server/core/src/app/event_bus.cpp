#include "app/event_bus.h"
#include <algorithm>

namespace guard {

namespace {
constexpr std::size_t kTransientCapacity = 300;
}

EventBus::EventBus(std::size_t capacity)
    : capacity_(capacity),
      nextId_(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count())) {}

uint64_t EventBus::publish(const std::string& type, const nlohmann::json& data, const std::string& topic) {
    uint64_t id;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        id = nextId_++;
        nlohmann::json envelope{{"type", type}, {"data", data}};
        events_.push_back({id, type, topic, envelope.dump()});
        while (events_.size() > capacity_) events_.pop_front();
    }
    cv_.notify_all();
    return id;
}

uint64_t EventBus::publishTransient(const std::string& type, const nlohmann::json& data, const std::string& topic) {
    uint64_t id;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        id = nextId_++;
        nlohmann::json envelope{{"type", type}, {"data", data}};
        transient_.push_back({id, type, topic, envelope.dump()});
        while (transient_.size() > kTransientCapacity) transient_.pop_front();
    }
    cv_.notify_all();
    return id;
}

std::vector<BusEvent> EventBus::waitAfter(uint64_t afterId, std::chrono::milliseconds timeout,
                                          const std::string& topicFilter) {
    std::unique_lock<std::mutex> lk(mtx_);
    auto collect = [&] {
        std::vector<BusEvent> out;
        for (const auto* q : {&events_, &transient_}) {
            for (const auto& e : *q) {
                if (e.id <= afterId) continue;
                if (!topicFilter.empty() && e.topic != topicFilter) continue;
                out.push_back(e);
            }
        }
        std::sort(out.begin(), out.end(), [](const BusEvent& a, const BusEvent& b) { return a.id < b.id; });
        return out;
    };
    std::vector<BusEvent> out = collect();
    if (!out.empty()) return out;
    const uint64_t seen = nextId_;
    cv_.wait_for(lk, timeout, [&] { return nextId_ != seen; });
    return collect();
}

uint64_t EventBus::lastId() {
    std::lock_guard<std::mutex> lk(mtx_);
    return nextId_ - 1;
}

} // namespace guard
