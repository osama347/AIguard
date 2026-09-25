#pragma once
#include "domain/access_policy.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace guard {

struct VisitConfig {
    int64_t gapMs = 4000;        // nothing seen for this long ends a visit
    int64_t maxVisitMs = 60000;  // a visit longer than this is split (vehicle parked at the gate)
    int minFrames = 2;           // visits with fewer frames containing detections are noise
};

struct PlateObservation {
    std::string text;                  // normalized; may be empty (plate found, unreadable)
    std::optional<int64_t> vehicleId;
    std::string vehicleStatus;
    float confidence = 0.f;
};

// What one analysed live frame contributed, after face/plate matching.
struct FrameObservation {
    int64_t tsMs = 0;
    uint64_t frame = 0;                // inference frame number (for the snapshot)
    bool hasImage = false;             // inference kept a JPEG of this frame
    std::vector<std::optional<FaceMatch>> faces;
    std::vector<float> faceConfidences;
    std::vector<PlateObservation> plates;
    std::string overlayJson;           // boxes to draw on the snapshot

    bool empty() const { return faces.empty() && plates.empty(); }
    double score() const;              // how good a snapshot this frame makes
};

// One vehicle/person passing the camera: evidence from its first to last frame.
struct Visit {
    int64_t startMs = 0, endMs = 0;
    int frames = 0;                    // frames with detections
    Evidence evidence;
    std::optional<FrameObservation> best;   // best frame that has an image

    bool meaningful() const { return evidence.facesSeen() > 0 || !evidence.plates().empty(); }
};

// ---------------------------------------------------------------------------
// VisitTracker: turns a continuous stream of analysed frames into discrete
// visits. A visit opens on the first frame with a detection and closes after
// `gapMs` without any, or when it exceeds `maxVisitMs`. The access decision is
// then taken over the whole visit, exactly like over a whole uploaded clip.
// Not thread-safe: one tracker per camera, fed by one thread.
// ---------------------------------------------------------------------------
class VisitTracker {
public:
    explicit VisitTracker(VisitConfig cfg) : cfg_(cfg) {}

    // Feeds one frame. Returns a visit if one ended (by gap or length);
    // frames that are too few to count are discarded silently.
    std::optional<Visit> observe(const FrameObservation& obs);

    // Ends the current visit now (camera lost, service stopping).
    std::optional<Visit> flush();

    bool active() const { return current_.has_value(); }
    // Best snapshot candidate changed with the last observe() call.
    bool bestChanged() const { return bestChanged_; }
    const std::optional<Visit>& current() const { return current_; }

private:
    std::optional<Visit> close();

    VisitConfig cfg_;
    std::optional<Visit> current_;
    int64_t lastSeenMs_ = 0;
    bool bestChanged_ = false;
};

} // namespace guard
