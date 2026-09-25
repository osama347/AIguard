#pragma once
#include "domain/face_matcher.h"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace guard {

struct PolicyConfig {
    int minFaceFrames = 1;    // frames a driver must be matched in to count
    int minPlateReads = 1;    // OCR reads a registered plate needs to count
};

struct DriverTally {
    int64_t driverId = 0;
    std::string name;
    std::string status;
    int frames = 0;
    float bestSimilarity = 0.f;
};

struct PlateTally {
    std::string text;                      // normalized
    int reads = 0;
    std::optional<int64_t> vehicleId;      // set if the plate is registered
    std::string vehicleStatus;
};

// ---------------------------------------------------------------------------
// Evidence gathered over one job, frame by frame. Each observation is recorded
// once; the decision is taken over the whole clip so a single bad frame (a
// misread plate, a blurred face) does not decide the outcome.
// ---------------------------------------------------------------------------
class Evidence {
public:
    void addFace(const std::optional<FaceMatch>& match);
    void addPlate(const std::string& normalizedText, std::optional<int64_t> vehicleId,
                  const std::string& vehicleStatus);
    void addFrame() { ++frames_; }

    const std::map<int64_t, DriverTally>& drivers() const { return drivers_; }
    const std::map<std::string, PlateTally>& plates() const { return plates_; }
    int facesSeen() const { return facesSeen_; }
    int unknownFaces() const { return unknownFaces_; }
    int frames() const { return frames_; }

private:
    std::map<int64_t, DriverTally> drivers_;
    std::map<std::string, PlateTally> plates_;
    int facesSeen_ = 0, unknownFaces_ = 0, frames_ = 0;
};

// Verdicts: authorized | unauthorized | unknown_driver | unknown_vehicle | unknown_both
struct Decision {
    std::string verdict;
    bool authorized = false;
    bool blacklisted = false;
    std::optional<DriverTally> driver;
    std::optional<PlateTally> vehicle;
    std::string reason;                    // human-readable explanation
};

using AssignmentSet = std::set<std::pair<int64_t, int64_t>>;   // (driverId, vehicleId)

Decision decideAccess(const Evidence& evidence, const AssignmentSet& assignments,
                      const PolicyConfig& policy);

} // namespace guard
