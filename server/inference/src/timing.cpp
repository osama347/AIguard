#include "ai/timing.h"
#include <algorithm>
#include <cmath>

namespace ai {

void LatencyHistogram::add(double ms) {
    std::lock_guard<std::mutex> lock(mu_);
    samples_.push_back(ms);
}

double LatencyHistogram::percentile(double percent) const {
    std::lock_guard<std::mutex> lock(mu_);
    if (samples_.empty()) return -1.0;
    std::vector<double> s = samples_;
    std::sort(s.begin(), s.end());
    if (percent >= 100.0) return s.back();
    std::size_t idx = static_cast<std::size_t>(
        std::ceil((percent / 100.0) * static_cast<double>(s.size())) - 1.0);
    if (idx >= s.size()) idx = s.size() - 1;
    return s[idx];
}

std::size_t LatencyHistogram::count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return samples_.size();
}

void LatencyHistogram::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    samples_.clear();
}

void RequestTimer::begin(const std::string& stage) {
    activeStage_ = stage;
    stageStart_ = std::chrono::steady_clock::now();
}

double RequestTimer::end(const std::string& stage) {
    using namespace std::chrono;
    auto now = steady_clock::now();
    double ms = duration_cast<microseconds>(now - stageStart_).count() / 1000.0;
    (void)stage;  // activeStage_ tracking kept simple for single-stage-at-a-time MVP
    return ms;
}

void RequestTimer::markRequestDone() {
    using namespace std::chrono;
    // track from request start; for MVP the RequestTimer is created at handler entry
    // and lastTotalMs_ is populated from the collector. Keep simple.
}

} // namespace ai
