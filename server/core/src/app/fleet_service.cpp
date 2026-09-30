#include "app/fleet_service.h"
#include "app/errors.h"
#include "domain/plate.h"
#include "infra/crypto.h"
#include <cctype>

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

// A driver, but phone is mandatory (unlike drivers in general): this is how an admin
// reaches the owner, and how the authorization code gets shared with them.
void validateOwner(Driver& owner) {
    validateDriver(owner);
    const std::string p = trimmed(owner.phone);
    if (p.empty()) throw ValidationError("owner phone/WhatsApp number is required");
    // Pragmatic check, not a full E.164 library: optional leading '+', digits/spaces/dashes,
    // 7-15 digits overall.
    int digits = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const char c = p[i];
        if (c == '+' && i == 0) continue;
        if (c == ' ' || c == '-') continue;
        if (std::isdigit(static_cast<unsigned char>(c))) { ++digits; continue; }
        throw ValidationError("owner phone must contain only digits, spaces, dashes and an optional leading +");
    }
    if (digits < 7 || digits > 15) throw ValidationError("owner phone must have 7-15 digits");
    owner.phone = p;
}

// Short, human-shareable code (read aloud, pasted into WhatsApp): 8 symbols from an
// alphabet without visually ambiguous characters (0/O, 1/I/L), shown as "XXXX-XXXX".
// Not a login secret like a password, so no iteration/hashing — just enough entropy
// (32^8) that guessing is impractical, backed by the redemption rate limit in the API layer.
std::string generateAuthCode() {
    static const char* kAlphabet = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
    const auto bytes = crypto::randomBytes(8);
    std::string code;
    for (auto b : bytes) code += kAlphabet[b % 32];
    return code.substr(0, 4) + "-" + code.substr(4);
}

bool isAuthCodeCollision(const DbConstraintError& e) {
    return std::string(e.what()).find("auth_code") != std::string::npos;
}

bool isPlateCollision(const DbConstraintError& e) {
    return std::string(e.what()).find("plate_normalized") != std::string::npos;
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
    Driver d = getDriver(id);   // 404 if missing
    if (d.isOwner) throw ConflictError("reassign or delete their vehicle first: " + d.name + " owns a vehicle");
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

Driver FleetService::setDriverPhoto(int64_t driverId, const std::string& photoFile, const std::string& actor) {
    getDriver(driverId);   // 404 if missing
    fleet_.setDriverPhoto(driverId, photoFile);
    events_.audit(actor, photoFile.empty() ? "delete" : "update", "driver_photo", std::to_string(driverId));
    return getDriver(driverId);
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

Vehicle FleetService::createVehicleWithOwner(Vehicle v, Driver owner, const std::string& actor) {
    validateVehicle(v);
    validateOwner(owner);
    owner.isOwner = true;

    int64_t id = 0;
    for (int attempt = 0; ; ++attempt) {
        const std::string code = generateAuthCode();
        try {
            id = fleet_.createVehicleWithOwner(v, owner, code);
            break;
        } catch (const DbConstraintError& e) {
            if (isAuthCodeCollision(e) && attempt < 4) continue;   // astronomically rare; retry with a fresh code
            if (isPlateCollision(e)) throw ConflictError("a vehicle with plate " + v.plateNormalized + " already exists");
            throw;
        }
    }
    invalidate();
    events_.audit(actor, "create", "vehicle", std::to_string(id), v.plateNormalized + " owner=" + owner.name);
    return getVehicle(id);
}

Vehicle FleetService::setVehicleOwner(int64_t vehicleId, Driver owner, const std::string& actor) {
    Vehicle v = getVehicle(vehicleId);   // 404 if missing
    if (v.owner) throw ConflictError("vehicle already has an owner");
    validateOwner(owner);
    owner.isOwner = true;

    for (int attempt = 0; ; ++attempt) {
        const std::string code = generateAuthCode();
        try {
            fleet_.setVehicleOwner(vehicleId, owner, code);
            break;
        } catch (const DbConstraintError& e) {
            if (isAuthCodeCollision(e) && attempt < 4) continue;
            throw;
        }
    }
    invalidate();
    events_.audit(actor, "set_owner", "vehicle", std::to_string(vehicleId), owner.name);
    return getVehicle(vehicleId);
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

Vehicle FleetService::findVehicleByAuthCode(const std::string& code) {
    auto v = fleet_.vehicleByAuthCode(trimmed(code));
    if (!v) throw NotFoundError("authorization code not recognized");
    return *v;
}

Driver FleetService::authorizeDriverForVehicle(int64_t vehicleId, Driver d, const std::string& actor) {
    getVehicle(vehicleId);   // 404 if the vehicle went away between resolving the code and this call
    validateDriver(d);
    d.isOwner = false;
    const int64_t id = fleet_.createDriver(d);
    fleet_.assign(id, vehicleId);
    invalidate();
    events_.audit(actor, "authorize", "driver", std::to_string(id), d.name + " for vehicle " + std::to_string(vehicleId));
    return getDriver(id);
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
