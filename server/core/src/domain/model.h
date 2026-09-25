#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Domain entities. Plain data: no HTTP, SQL or JSON in this layer.
namespace guard {

struct VehicleRef { int64_t id = 0; std::string plateNumber; };
struct DriverRef { int64_t id = 0; std::string name; };

struct Driver {
    int64_t id = 0;
    std::string name;
    std::string status = "active";   // active | inactive | blacklisted
    std::string phone;
    std::string notes;
    std::string createdAt, updatedAt;
    int templateCount = 0;
    std::vector<VehicleRef> vehicles;
};

struct Vehicle {
    int64_t id = 0;
    std::string plateNumber;
    std::string plateNormalized;
    std::string make, model, color;
    std::string status = "active";   // active | inactive | blacklisted
    std::string createdAt, updatedAt;
    std::vector<DriverRef> drivers;
};

// One driver's enrolled face templates, as used by the matcher.
struct Identity {
    int64_t driverId = 0;
    std::string name;
    std::string status;
    std::vector<std::vector<float>> templates;
};

struct Job {
    std::string id;
    std::string status;              // queued | running | completed | failed | cancelled
    std::string source;
    std::string originalName;
    int64_t sizeBytes = 0;
    int attempts = 0;
    double progress = 0;
    std::string verdict;
    std::string resultJson;          // serialized Decision (see api layer)
    std::string error;
    std::string createdBy;
    std::string createdAt, startedAt, finishedAt;
};

struct Camera {
    int64_t id = 0;
    std::string name;
    std::string source;
    bool enabled = true;
    double sampleFps = 5.0;
    std::string createdAt, updatedAt;
};

struct Community {
    std::string name, address, city, country, helpline, email, website;
    std::string logoFile;            // inside <data>/media/branding/, "" = none
    std::string updatedAt;
    bool configured() const { return !name.empty(); }
};

struct AccessEvent {
    int64_t id = 0;
    std::string eventTime;           // start of the visit (camera) / completion (job)
    std::string endedAt;             // camera visits only
    std::string jobId;               // set for uploaded test videos
    std::optional<int64_t> cameraId; // set for live cameras
    std::string cameraName;          // joined for display
    std::string snapshot;            // path under media/, empty if none

    // Camera visits always have an end time; test videos never do. (camera_id
    // alone is not enough: it becomes NULL when the camera is deleted.)
    bool fromCamera() const { return cameraId.has_value() || !endedAt.empty(); }
    std::string verdict;
    bool authorized = false;
    std::optional<int64_t> driverId, vehicleId;
    std::string driverName;          // joined for display
    std::string plateText;
    std::string detailsJson;
};

struct Alert {
    int64_t id = 0;
    std::string createdAt;
    std::string severity;            // info | warning | critical
    std::string kind;                // unauthorized | blacklisted | unknown_driver | camera_offline | system
    std::string message;
    std::optional<int64_t> accessEventId;
    std::string acknowledgedAt, resolvedAt;
    std::string acknowledgedBy, resolvedBy;   // usernames ("system" for automatic)
};

// Roles: "admin" manages everything; "guard" monitors (live view, access log,
// alerts) and sees the fleet read-only.
struct User {
    int64_t id = 0;
    std::string username;
    std::string passwordHash;
    std::string role = "guard";
    std::string createdAt;
    std::string fullName;
    bool active = true;
    std::string lastLoginAt;

    bool isAdmin() const { return role == "admin"; }
};

} // namespace guard
