#include "domain/visit_tracker.h"

namespace guard {

double FrameObservation::score() const {
    // Readable plates matter most (they identify the vehicle), then faces.
    double s = 0;
    for (const auto& p : plates) s += p.text.empty() ? 0.2 * p.confidence : 1.0 + p.confidence;
    for (float c : faceConfidences) s += 0.8 * c;
    for (const auto& f : faces) if (f) s += 0.5;
    return s;
}

std::optional<Visit> VisitTracker::observe(const FrameObservation& obs) {
    bestChanged_ = false;
    std::optional<Visit> ended;

    if (current_) {
        const bool idleTooLong = obs.tsMs - lastSeenMs_ > cfg_.gapMs;
        const bool tooLong = obs.tsMs - current_->startMs >= cfg_.maxVisitMs;
        if (idleTooLong || (tooLong && !obs.empty())) ended = close();
    }
    if (obs.empty()) return ended;

    if (!current_) {
        current_.emplace();
        current_->startMs = obs.tsMs;
    }
    Visit& v = *current_;
    v.endMs = obs.tsMs;
    ++v.frames;
    v.evidence.addFrame();
    for (const auto& f : obs.faces) v.evidence.addFace(f);
    for (const auto& p : obs.plates) v.evidence.addPlate(p.text, p.vehicleId, p.vehicleStatus);
    lastSeenMs_ = obs.tsMs;

    if (obs.hasImage && (!v.best || obs.score() > v.best->score())) {
        v.best = obs;
        bestChanged_ = true;
    }
    return ended;
}

std::optional<Visit> VisitTracker::flush() {
    bestChanged_ = false;
    return current_ ? close() : std::nullopt;
}

std::optional<Visit> VisitTracker::close() {
    Visit v = std::move(*current_);
    current_.reset();
    if (v.frames < cfg_.minFrames) return std::nullopt;
    return v;
}

} // namespace guard
