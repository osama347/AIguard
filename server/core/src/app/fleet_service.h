#pragma once
#include "app/frame_annotator.h"
#include "common/config.h"
#include "domain/access_policy.h"
#include "domain/face_matcher.h"
#include "infra/inference_client.h"
#include "infra/repositories.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace guard {

struct PhotoOutcome {
    std::string filename;
    bool enrolled = false;
    std::string error;          // no_face | invalid_image | ...
};

// ---------------------------------------------------------------------------
// FleetService: drivers, vehicles, assignments and face enrollment.
// Validates input, writes the audit log, and keeps a cached FaceMatcher that
// is rebuilt lazily after any change to identities.
// ---------------------------------------------------------------------------
class FleetService {
public:
    FleetService(FleetRepository& fleet, EventRepository& events, AuthorizationRepository& authz,
                 const CoreConfig& cfg, InferenceClient& inference, float matchThreshold);

    std::vector<Driver> listDrivers() { return fleet_.listDrivers(); }
    Driver getDriver(int64_t id);
    Driver createDriver(Driver d, const std::string& actor);
    Driver updateDriver(Driver d, const std::string& actor);
    void deleteDriver(int64_t id, const std::string& actor);

    // Embeds each photo through guard-inference and stores the templates.
    std::vector<PhotoOutcome> enrollPhotos(int64_t driverId,
                                           const std::vector<std::pair<std::string, std::string>>& photos,
                                           const std::string& actor);   // (filename, bytes)
    void clearPhotos(int64_t driverId, const std::string& actor);
    // Sets or clears (photoFile = "") a driver's profile picture; the caller has
    // already written/removed the file itself. Cosmetic only — never touches the
    // face matcher, so no invalidate().
    Driver setDriverPhoto(int64_t driverId, const std::string& photoFile, const std::string& actor);

    std::vector<Vehicle> listVehicles() { return fleet_.listVehicles(); }
    Vehicle getVehicle(int64_t id);
    Vehicle createVehicle(Vehicle v, const std::string& actor);
    Vehicle updateVehicle(Vehicle v, const std::string& actor);
    void deleteVehicle(int64_t id, const std::string& actor);

    // Registers a vehicle together with its owner: the owner is created as a driver
    // (auto-enrolled, is_owner=true), auto-assigned, and given a fresh authorization
    // code, all atomically. Owner name/phone are validated the same as any driver,
    // plus phone is mandatory.
    Vehicle createVehicleWithOwner(Vehicle v, Driver owner, const std::string& actor);
    // Backfills an owner (same validation/atomicity) onto a vehicle that doesn't have one yet.
    Vehicle setVehicleOwner(int64_t vehicleId, Driver owner, const std::string& actor);
    // Resolves an authorization code to its vehicle; throws NotFoundError if unrecognized
    // (callers should rate-limit on that, the code space is much smaller than a password).
    Vehicle findVehicleByAuthCode(const std::string& code);
    // Adds a new authorized (non-owner) driver to a vehicle already resolved from a code.
    Driver authorizeDriverForVehicle(int64_t vehicleId, Driver d, const std::string& actor);

    void assign(int64_t driverId, int64_t vehicleId, const std::string& actor);
    void unassign(int64_t driverId, int64_t vehicleId, const std::string& actor);

    // ------------------------------------------------- owner-approved requests
    // Captures a driver's details (and optional face photos) and mints a single-use
    // token; the driver is NOT created until the vehicle's owner confirms. `token`
    // in the result is the raw one-time secret (only available here) that goes into
    // the confirmation link; only its SHA-256 is stored.
    struct AuthorizationCreated {
        DriverAuthorization authorization;
        std::string token;
    };
    AuthorizationCreated createAuthorization(int64_t vehicleId, Driver driver,
        const std::vector<std::pair<std::string, std::string>>& photos,
        int expiresHours, const std::string& actor);

    std::vector<DriverAuthorization> listAuthorizations(int64_t vehicleId, bool pendingOnly);
    std::optional<DriverAuthorization> authorization(int64_t id);
    // Resolves a link token (marking it expired lazily); nullopt if unrecognized.
    std::optional<DriverAuthorization> authorizationByToken(const std::string& token);
    void cancelAuthorization(int64_t id, const std::string& actor);

    // Applies the owner's decision. On approval the driver is created, assigned,
    // and their staged photos enrolled (best effort). Throws ConflictError if the
    // request was already decided or has expired (the row is marked expired).
    struct AuthorizationResult {
        std::string status;                    // approved | denied
        std::optional<Driver> driver;
        std::vector<PhotoOutcome> photos;
        bool photosPending = false;            // staged photos exist but couldn't be enrolled
    };
    AuthorizationResult decideAuthorization(const std::string& token, bool approve, const std::string& actor);

    // Fleet data for interpreting frames (one job, or one camera for a while).
    std::shared_ptr<const FaceMatcher> matcher();
    FleetSnapshot snapshot();
    // Increases on every change to drivers, vehicles, templates or assignments.
    uint64_t version() const { return version_; }

private:
    void invalidate() { ++version_; }
    std::string pendingDir(const std::string& name) const;
    void removeStagedPhotos(const std::string& dir);

    FleetRepository& fleet_;
    EventRepository& events_;
    AuthorizationRepository& authz_;
    const CoreConfig& cfg_;
    InferenceClient& inference_;
    float matchThreshold_;

    std::atomic<uint64_t> version_{1};
    std::mutex matcherMtx_;
    uint64_t matcherVersion_ = 0;
    std::shared_ptr<const FaceMatcher> matcher_;
};

} // namespace guard
