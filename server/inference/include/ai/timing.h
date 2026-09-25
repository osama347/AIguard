#pragma once
#include <chrono>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace ai {

// Simple RAII stopwatch used to time individual pipeline stages.
class Stopwatch {
public:
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}
    // Elapsed milliseconds since construction.
    [[nodiscard]] double elapsedMs() const {
        using namespace std::chrono;
        return duration_cast<microseconds>(steady_clock::now() - start_).count() / 1000.0;
    }
    void reset() { start_ = std::chrono::steady_clock::now(); }
private:
    std::chrono::steady_clock::time_point start_;
};

// Lightweight latency histogram for aggregated metrics (P50/P95/P99).
class LatencyHistogram {
public:
    void add(double ms);
    // percent in [0,100]; returns -1 if empty.
    [[nodiscard]] double percentile(double percent) const;
    [[nodiscard]] std::size_t count() const;
    void clear();
private:
    mutable std::mutex mu_;
    std::vector<double> samples_;
};

// Per-request timing collector, thread-local so each HTTP request gets its own.
class RequestTimer {
public:
    void begin(const std::string& stage);           // start timing a stage
    double end(const std::string& stage);           // record stage duration (ms)
    void markRequestDone();                          // record total request time
    // total request time in ms (set by markRequestDone)
    [[nodiscard]] double totalMs() const { return lastTotalMs_; }
private:
    std::chrono::steady_clock::time_point stageStart_;
    std::string activeStage_;
    double lastTotalMs_ = 0.0;
};

} // namespace ai
