#include "app/fleet_service.h"
#include "app/errors.h"
#include "domain/plate.h"

namespace guard {

namespace {

// Identifies how enrolled embeddings were produced (model, 512-d, input standardization
// (x-127.5)/128, 15% crop margin; measured in tools/face_eval.py). Templates carrying any
// other tag are not comparable with today's embeddings and are ignored until re-enrolled.
// Change this whenever the inference-side face preprocessing changes.
const std::string kFaceTemplateVersion = "facenet512/std128/margin0.15";

const char* kStatuses[] = {"active", "inactive", "blacklisted"};

void validateStatus(const std::string& s) {
    for (const char* v : kStatuses) if (s == v) return;
    throw ValidationError("status must be one of: active, inactive, blacklisted");
}

std::string trimmed(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

void validateDriver(Driver& d) {
    d.name = trimmed(d.name);
    if (d.name.empty() || d.name.size() > 120) throw ValidationError("name is required (max 120 characters)");
    if (d.phone.size() > 40) throw ValidationError("phone is too long");
    if (d.notes.size() > 2000) throw ValidationError("notes are too long");
    validateStatus(d.status);
}

void validateVehicle(Vehicle& v) {
    v.plateNumber = trimmed(v.plateNumber);
    v.plateNormalized = normalizePlate(v.plateNumber);
    if (v.plateNormalized.empty() || v.plateNormalized.size() > 16)
        throw ValidationError("plate_number must contain 1-16 letters or digits");
    for (auto* f : {&v.make, &v.model, &v.color})
        if (f->size() > 60) throw ValidationError("make/model/color are limited to 60 characters");
    validateStatus(v.status);
}

} // namespace

FleetService::FleetService(FleetRepository& fleet, EventRepository& events, InferenceClient& inference,
                           float matchThreshold)
    : fleet_(fleet), events_(events), inference_(inference), matchThreshold_(matchThreshold) {}

// ---------------------------------------------------------------- drivers

Driver FleetService::getDriver(int64_t id) {
    auto d = fleet_.getDriver(id);
    if (!d) throw NotFoundError("driver " + std::to_string(id) + " not found");
    return *d;
}

Driver FleetService::createDriver(Driver d, const std::string& actor) {
    validateDriver(d);
    const int64_t id = fleet_.createDriver(d);
    events_.audit(actor, "create", "driver", std::to_string(id), d.name);
    return getDriver(id);
}

Driver FleetService::updateDriver(Driver d, const std::string& actor) {
    validateDriver(d);
    if (!fleet_.updateDriver(d)) throw NotFoundError("driver " + std::to_string(d.id) + " not found");
    events_.audit(actor, "update", "driver", std::to_string(d.id), d.name + " status=" + d.status);
    invalidate();   // name/status are part of the matcher snapshot
    return getDriver(d.id);
}

void FleetService::deleteDriver(int64_t id, const std::string& actor) {
    if (!fleet_.deleteDriver(id)) throw NotFoundError("driver " + std::to_string(id) + " not found");
    events_.audit(actor, "delete", "driver", std::to_string(id));
    invalidate();
}

std::vector<PhotoOutcome> FleetService::enrollPhotos(
    int64_t driverId, const std::vector<std::pair<std::string, std::string>>& photos, const std::string& actor) {
    getDriver(driverId);   // 404 if missing
    if (photos.empty()) throw ValidationError("at least one photo is required");
    if (photos.size() > 20) throw ValidationError("at most 20 photos per request");

    std::vector<std::string> bytes;
    for (const auto& p : photos) bytes.push_back(p.second);

    std::vector<EmbedResult> results;
    try {
        results = inference_.embed(bytes);
    } catch (const InferenceUnavailable&) {
        throw UnavailableError("the AI engine is not running");
    } catch (const InferenceError& e) {
        if (e.status() == 503) throw UnavailableError("the AI engine is still loading its models");
        throw;
    }

    std::vector<PhotoOutcome> out;
    std::vector<std::vector<float>> templates;
    for (std::size_t i = 0; i < photos.size(); ++i) {
        PhotoOutcome o{photos[i].first, false, ""};
        if (results[i].embedding) {
            templates.push_back(*results[i].embedding);
            o.enrolled = true;
        } else {
            o.error = results[i].error;
        }
        out.push_back(o);
    }
    if (!templates.empty()) {
        fleet_.addTemplates(driverId, templates, kFaceTemplateVersion);
        events_.audit(actor, "enroll", "driver", std::to_string(driverId),
                      std::to_string(templates.size()) + " photo(s)");
        invalidate();
    }
    return out;
}

void FleetService::clearPhotos(int64_t driverId, const std::string& actor) {
    getDriver(driverId);
    fleet_.clearTemplates(driverId);
    events_.audit(actor, "clear_photos", "driver", std::to_string(driverId));
    invalidate();
}

// ---------------------------------------------------------------- vehicles

Vehicle FleetService::getVehicle(int64_t id) {
    auto v = fleet_.getVehicle(id);
    if (!v) throw NotFoundError("vehicle " + std::to_string(id) + " not found");
    return *v;
}

Vehicle FleetService::createVehicle(Vehicle v, const std::string& actor) {
    validateVehicle(v);
    int64_t id;
    try {
        id = fleet_.createVehicle(v);
    } catch (const DbConstraintError&) {
        throw ConflictError("a vehicle with plate " + v.plateNormalized + " already exists");
    }
    invalidate();
    events_.audit(actor, "create", "vehicle", std::to_string(id), v.plateNormalized);
    return getVehicle(id);
}

Vehicle FleetService::updateVehicle(Vehicle v, const std::string& actor) {
    validateVehicle(v);
    try {
        if (!fleet_.updateVehicle(v)) throw NotFoundError("vehicle " + std::to_string(v.id) + " not found");
    } catch (const DbConstraintError&) {
        throw ConflictError("a vehicle with plate " + v.plateNormalized + " already exists");
    }
    invalidate();
    events_.audit(actor, "update", "vehicle", std::to_string(v.id), v.plateNormalized + " status=" + v.status);
    return getVehicle(v.id);
}

void FleetService::deleteVehicle(int64_t id, const std::string& actor) {
    if (!fleet_.deleteVehicle(id)) throw NotFoundError("vehicle " + std::to_string(id) + " not found");
    invalidate();
    events_.audit(actor, "delete", "vehicle", std::to_string(id));
}

void FleetService::assign(int64_t driverId, int64_t vehicleId, const std::string& actor) {
    getDriver(driverId);
    getVehicle(vehicleId);
    fleet_.assign(driverId, vehicleId);
    invalidate();
    events_.audit(actor, "assign", "driver", std::to_string(driverId), "vehicle " + std::to_string(vehicleId));
}

void FleetService::unassign(int64_t driverId, int64_t vehicleId, const std::string& actor) {
    if (!fleet_.unassign(driverId, vehicleId)) throw NotFoundError("assignment not found");
    invalidate();
    events_.audit(actor, "unassign", "driver", std::to_string(driverId), "vehicle " + std::to_string(vehicleId));
}

// ---------------------------------------------------------------- matching

std::shared_ptr<const FaceMatcher> FleetService::matcher() {
    std::lock_guard<std::mutex> lk(matcherMtx_);
    const uint64_t v = version_;
    if (!matcher_ || matcherVersion_ != v) {
        matcher_ = std::make_shared<const FaceMatcher>(fleet_.identities(kFaceTemplateVersion), matchThreshold_);
        matcherVersion_ = v;
    }
    return matcher_;
}

FleetSnapshot FleetService::snapshot() {
    FleetSnapshot s;
    s.matcher = matcher();
    s.assignments = fleet_.assignments();
    for (const auto& v : fleet_.listVehicles()) s.vehicles[v.plateNormalized] = {v.id, v.status};
    return s;
}

} // namespace guard
