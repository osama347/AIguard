#include "app/fleet_service.h"
#include "app/errors.h"
#include "common/log.h"
#include "domain/plate.h"
#include "infra/crypto.h"
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace guard {

namespace {

// "YYYY-MM-DD HH:MM:SS" in UTC — the same shape SQLite's datetime() produces, so
// the two compare correctly as plain strings.
std::string utcStamp(std::time_t t) {
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[20];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

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

FleetService::FleetService(FleetRepository& fleet, EventRepository& events, AuthorizationRepository& authz,
                           const CoreConfig& cfg, InferenceClient& inference, float matchThreshold)
    : fleet_(fleet), events_(events), authz_(authz), cfg_(cfg), inference_(inference),
      matchThreshold_(matchThreshold) {}

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

// ------------------------------------------------ owner-approved requests

std::string FleetService::pendingDir(const std::string& name) const {
    return cfg_.mediaDir() + "/pending/" + name;
}

void FleetService::removeStagedPhotos(const std::string& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    fs::remove_all(pendingDir(dir), ec);   // best effort
}

FleetService::AuthorizationCreated FleetService::createAuthorization(
    int64_t vehicleId, Driver driver, const std::vector<std::pair<std::string, std::string>>& photos,
    int expiresHours, const std::string& actor) {
    const Vehicle v = getVehicle(vehicleId);   // 404 if missing
    if (!v.owner) throw ValidationError("this vehicle has no owner to approve a driver");
    validateDriver(driver);
    driver.isOwner = false;
    if (photos.size() > 20) throw ValidationError("at most 20 photos per request");

    const std::string token = crypto::randomHex(16);   // 128 bits; goes in the link, never stored raw
    DriverAuthorization a;
    a.vehicleId = vehicleId;
    a.driverName = driver.name;
    a.driverPhone = trimmed(driver.phone);
    a.tokenHash = crypto::sha256Hex(token);
    a.requestedBy = actor;
    a.expiresAt = utcStamp(std::time(nullptr) + static_cast<std::time_t>(expiresHours) * 3600);
    const int64_t id = authz_.create(a);

    // Stage face photos under <data>/media/pending/<token hash> so the owner's approval
    // can enrol them without the admin present. The directory name is the token hash;
    // the raw token never touches disk.
    if (!photos.empty()) {
        const std::string dir = token.substr(0, 16) + crypto::randomHex(8);
        std::error_code ec;
        fs::create_directories(pendingDir(dir), ec);
        for (std::size_t i = 0; i < photos.size(); ++i) {
            std::ofstream out(pendingDir(dir) + "/" + std::to_string(i) + ".img", std::ios::binary | std::ios::trunc);
            if (!out) continue;
            out.write(photos[i].second.data(), static_cast<std::streamsize>(photos[i].second.size()));
        }
        authz_.setPhotoDir(id, dir);
    }

    events_.audit(actor, "request_authorization", "vehicle", std::to_string(vehicleId),
                  a.driverName + " (awaiting owner)");

    AuthorizationCreated out;
    out.authorization = *authz_.get(id);
    out.token = token;
    return out;
}

std::vector<DriverAuthorization> FleetService::listAuthorizations(int64_t vehicleId, bool pendingOnly) {
    auto rows = authz_.list(vehicleId, pendingOnly);
    const std::string now = utcStamp(std::time(nullptr));
    for (auto& a : rows)   // surface expiry without a background job
        if (a.status == "pending" && a.expiresAt < now) { authz_.decide(a.id, "expired"); a.status = "expired"; }
    return rows;
}

std::optional<DriverAuthorization> FleetService::authorization(int64_t id) {
    auto a = authz_.get(id);
    if (a && a->status == "pending" && a->expiresAt < utcStamp(std::time(nullptr))) {
        authz_.decide(a->id, "expired");
        a->status = "expired";
    }
    return a;
}

std::optional<DriverAuthorization> FleetService::authorizationByToken(const std::string& token) {
    if (token.empty()) return std::nullopt;
    auto a = authz_.byTokenHash(crypto::sha256Hex(token));
    if (a && a->status == "pending" && a->expiresAt < utcStamp(std::time(nullptr))) {
        authz_.decide(a->id, "expired");
        a->status = "expired";
    }
    return a;
}

void FleetService::cancelAuthorization(int64_t id, const std::string& actor) {
    auto a = authorization(id);   // 404 if missing, marks expiry
    if (!a) throw NotFoundError("authorization request " + std::to_string(id) + " not found");
    if (a->status != "pending") throw ConflictError("this request is already " + a->status);
    authz_.decide(id, "cancelled");
    removeStagedPhotos(a->photoDir);
    events_.audit(actor, "cancel_authorization", "vehicle", std::to_string(a->vehicleId), a->driverName);
}

FleetService::AuthorizationResult FleetService::decideAuthorization(
    const std::string& token, bool approve, const std::string& actor) {
    auto a = authorizationByToken(token);
    if (!a) throw NotFoundError("this link is not valid");
    if (a->status == "expired") throw ConflictError("this link has expired");
    if (a->status != "pending") throw ConflictError("this request was already " + a->status);

    AuthorizationResult out;
    if (!approve) {
        if (!authz_.decide(a->id, "denied")) throw ConflictError("this request was already decided");
        removeStagedPhotos(a->photoDir);
        events_.audit(actor, "deny_authorization", "vehicle", std::to_string(a->vehicleId), a->driverName);
        out.status = "denied";
        return out;
    }

    // Create the driver first; if another decision won the race, undo and bail.
    Driver d;
    d.name = a->driverName;
    d.phone = a->driverPhone;
    d.isOwner = false;
    validateDriver(d);
    const int64_t driverId = fleet_.createDriver(d);
    fleet_.assign(driverId, a->vehicleId);

    // Enrol the staged photos (best effort — the AI engine may be down right now).
    if (!a->photoDir.empty()) {
        std::vector<std::pair<std::string, std::string>> photos;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(pendingDir(a->photoDir), ec)) {
            std::ifstream in(e.path(), std::ios::binary);
            if (!in) continue;
            std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            photos.emplace_back(e.path().filename().string(), std::move(bytes));
        }
        if (!photos.empty()) {
            try {
                out.photos = enrollPhotos(driverId, photos, actor);
            } catch (const std::exception& e) {
                out.photosPending = true;
                LOG(Warn) << "could not enrol staged photos for driver " << driverId << ": " << e.what();
            }
        }
        removeStagedPhotos(a->photoDir);
    }

    if (!authz_.decide(a->id, "approved", driverId)) {   // lost the race: undo
        fleet_.deleteDriver(driverId);
        throw ConflictError("this request was already decided");
    }
    invalidate();
    events_.audit(actor, "approve_authorization", "driver", std::to_string(driverId),
                  d.name + " for vehicle " + std::to_string(a->vehicleId) + " (owner-approved)");
    out.status = "approved";
    out.driver = getDriver(driverId);
    return out;
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
