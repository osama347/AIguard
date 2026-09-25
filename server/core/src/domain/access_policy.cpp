#include "domain/access_policy.h"

namespace guard {

void Evidence::addFace(const std::optional<FaceMatch>& match) {
    ++facesSeen_;
    if (!match) { ++unknownFaces_; return; }
    auto& t = drivers_[match->driverId];
    t.driverId = match->driverId;
    t.name = match->name;
    t.status = match->status;
    ++t.frames;
    if (match->similarity > t.bestSimilarity) t.bestSimilarity = match->similarity;
}

void Evidence::addPlate(const std::string& normalizedText, std::optional<int64_t> vehicleId,
                        const std::string& vehicleStatus) {
    if (normalizedText.empty()) return;
    auto& t = plates_[normalizedText];
    t.text = normalizedText;
    t.vehicleId = vehicleId;
    t.vehicleStatus = vehicleStatus;
    ++t.reads;
}

namespace {

std::optional<DriverTally> strongestDriver(const Evidence& e, int minFrames) {
    std::optional<DriverTally> best;
    for (const auto& [id, t] : e.drivers()) {
        if (t.frames < minFrames) continue;
        if (!best || t.frames > best->frames ||
            (t.frames == best->frames && t.bestSimilarity > best->bestSimilarity))
            best = t;
    }
    return best;
}

std::optional<PlateTally> strongestVehicle(const Evidence& e, int minReads) {
    std::optional<PlateTally> best;
    for (const auto& [text, t] : e.plates()) {
        if (!t.vehicleId || t.reads < minReads) continue;
        if (!best || t.reads > best->reads) best = t;
    }
    return best;
}

} // namespace

Decision decideAccess(const Evidence& evidence, const AssignmentSet& assignments,
                      const PolicyConfig& policy) {
    Decision d;
    d.driver = strongestDriver(evidence, policy.minFaceFrames);
    d.vehicle = strongestVehicle(evidence, policy.minPlateReads);

    const bool driverBlacklisted = d.driver && d.driver->status == "blacklisted";
    const bool vehicleBlacklisted = d.vehicle && d.vehicle->vehicleStatus == "blacklisted";
    if (driverBlacklisted || vehicleBlacklisted) {
        d.verdict = "unauthorized";
        d.blacklisted = true;
        d.reason = driverBlacklisted ? "Driver " + d.driver->name + " is blacklisted"
                                     : "Vehicle " + d.vehicle->text + " is blacklisted";
        return d;
    }

    if (d.driver && d.vehicle) {
        if (d.driver->status != "active") {
            d.verdict = "unauthorized";
            d.reason = "Driver " + d.driver->name + " is " + d.driver->status;
        } else if (d.vehicle->vehicleStatus != "active") {
            d.verdict = "unauthorized";
            d.reason = "Vehicle " + d.vehicle->text + " is " + d.vehicle->vehicleStatus;
        } else if (!assignments.count({d.driver->driverId, *d.vehicle->vehicleId})) {
            d.verdict = "unauthorized";
            d.reason = d.driver->name + " is not assigned to vehicle " + d.vehicle->text;
        } else {
            d.verdict = "authorized";
            d.authorized = true;
            d.reason = d.driver->name + " is assigned to vehicle " + d.vehicle->text;
        }
    } else if (d.vehicle) {
        d.verdict = "unknown_driver";
        d.reason = evidence.facesSeen() ? "Vehicle " + d.vehicle->text + " recognized, driver not registered"
                                        : "Vehicle " + d.vehicle->text + " recognized, no face visible";
    } else if (d.driver) {
        d.verdict = "unknown_vehicle";
        d.reason = evidence.plates().empty() ? "Driver " + d.driver->name + " recognized, no plate read"
                                             : "Driver " + d.driver->name + " recognized, plate not registered";
    } else {
        d.verdict = "unknown_both";
        d.reason = "No registered driver or vehicle recognized";
    }
    return d;
}

} // namespace guard
