#pragma once
#include "domain/access_policy.h"
#include "domain/face_matcher.h"
#include "domain/visit_tracker.h"
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
#include <string>

namespace guard {

// Fleet data needed to interpret frames, captured once so per-frame work does
// no database access.
struct FleetSnapshot {
    struct VehicleInfo { int64_t id; std::string status; };
    std::shared_ptr<const FaceMatcher> matcher;
    AssignmentSet assignments;
    std::map<std::string, VehicleInfo> vehicles;   // normalized plate -> vehicle
};

struct AnnotatedFrame {
    nlohmann::json faces = nlohmann::json::array();    // [{bbox, conf, driver_id?, driver_name?, similarity?}]
    nlohmann::json plates = nlohmann::json::array();   // [{bbox, conf, text, text_conf, vehicle_id?}]
    FrameObservation observation;                      // same content, for evidence
};

// Matches the faces and plates of one inference "frame" event against the fleet.
// OCR reads shorter than 3 characters are kept for display but carry no identity.
AnnotatedFrame annotateFrame(const nlohmann::json& frameEvent, const FleetSnapshot& fleet);

} // namespace guard
