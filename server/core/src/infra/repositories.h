#pragma once
#include "domain/access_policy.h"
#include "domain/model.h"
#include "infra/sqlite_db.h"
#include <optional>
#include <string>
#include <vector>

namespace guard {

// ---------------------------------------------------------------------------
// SQLite repositories. Thin: SQL in, domain structs out. Constraint
// violations surface as DbConstraintError.
// ---------------------------------------------------------------------------

class FleetRepository {
public:
    explicit FleetRepository(Db& db) : db_(db) {}

    std::vector<Driver> listDrivers();
    std::optional<Driver> getDriver(int64_t id);
    int64_t createDriver(const Driver& d);
    bool updateDriver(const Driver& d);
    bool deleteDriver(int64_t id);

    void addTemplates(int64_t driverId, const std::vector<std::vector<float>>& embeddings,
                      const std::string& modelVersion);
    int clearTemplates(int64_t driverId);
    // Every driver with >= 1 template of this model_version. Templates made with a
    // different preprocessing/model are not comparable, so they are never loaded.
    std::vector<Identity> identities(const std::string& modelVersion);

    std::vector<Vehicle> listVehicles();
    std::optional<Vehicle> getVehicle(int64_t id);
    std::optional<Vehicle> vehicleByPlate(const std::string& normalized);
    int64_t createVehicle(const Vehicle& v);
    bool updateVehicle(const Vehicle& v);
    bool deleteVehicle(int64_t id);

    bool assign(int64_t driverId, int64_t vehicleId);
    bool unassign(int64_t driverId, int64_t vehicleId);
    AssignmentSet assignments();

private:
    void loadDriverVehicles(Driver& d);
    void loadVehicleDrivers(Vehicle& v);
    Db& db_;
};

class JobRepository {
public:
    explicit JobRepository(Db& db) : db_(db) {}

    void create(const Job& job);
    std::optional<Job> get(const std::string& id);
    std::vector<Job> list(int limit, int offset);
    std::optional<Job> nextQueued();
    int queuedAhead(const std::string& id);

    void markRunning(const std::string& id);     // attempts += 1, started_at = now
    void setProgress(const std::string& id, double progress);
    void complete(const std::string& id, const std::string& verdict, const std::string& resultJson);
    void fail(const std::string& id, const std::string& error);
    void cancel(const std::string& id);
    void requeue(const std::string& id);
    bool remove(const std::string& id);
    int requeueInterrupted();                   // running -> queued (after a core restart)

private:
    Db& db_;
};

class CameraRepository {
public:
    explicit CameraRepository(Db& db) : db_(db) {}

    std::vector<Camera> list();
    std::optional<Camera> get(int64_t id);
    int64_t create(const Camera& c);
    bool update(const Camera& c);
    bool remove(int64_t id);

private:
    Db& db_;
};

class CommunityRepository {
public:
    explicit CommunityRepository(Db& db) : db_(db) {}
    Community get();
    void save(const Community& c);            // replaces every field, including logoFile

private:
    Db& db_;
};

struct AccessEventFilter {
    std::optional<int64_t> cameraId;
    std::string source;              // "" | "camera" | "video"
    std::string verdict;             // "" = any
};

class EventRepository {
public:
    explicit EventRepository(Db& db) : db_(db) {}

    int64_t insertAccessEvent(const AccessEvent& e);
    std::optional<AccessEvent> getAccessEvent(int64_t id);
    std::vector<AccessEvent> listAccessEvents(int limit, int offset, const AccessEventFilter& filter = {});
    void extendAccessEvent(int64_t id, const std::string& endedAt);   // repeated sighting of the same visit
    int clearSnapshots(const std::string& pathPrefix);                // after the files were deleted

    Alert insertAlert(const std::string& severity, const std::string& kind, const std::string& message,
                      std::optional<int64_t> accessEventId);
    std::vector<Alert> listAlerts(bool openOnly, int limit);
    std::optional<Alert> getAlert(int64_t id);
    bool acknowledge(int64_t id, const std::string& by);
    bool resolve(int64_t id, const std::string& by);

    void audit(const std::string& actor, const std::string& action, const std::string& entity,
               const std::string& entityId, const std::string& details = "");

private:
    Db& db_;
};

// Aggregates for the admin dashboard. Camera visits only (test videos excluded);
// buckets are in the device's local time zone.
struct VerdictBucket { std::string bucket; std::string verdict; int count = 0; };
struct PlateCount { std::string plate; int count = 0; std::string lastSeen; std::string camera; };
struct DriverCount { int64_t driverId = 0; std::string name; int visits = 0; int authorized = 0; };
struct AlertStats {
    int open = 0, openCritical = 0, unacknowledged = 0;
    std::vector<double> responseSeconds;       // created -> acknowledged by a person, last 7 days
};

class StatsRepository {
public:
    explicit StatsRepository(Db& db) : db_(db) {}

    std::vector<VerdictBucket> hourly(int hours);          // bucket "YYYY-MM-DD HH:00"
    std::vector<VerdictBucket> daily(int days);            // bucket "YYYY-MM-DD", today included
    std::vector<std::pair<int64_t, int>> visitsTodayByCamera();
    std::vector<PlateCount> unregisteredPlates(int days, int limit);
    std::vector<DriverCount> topDrivers(int days, int limit);
    AlertStats alerts();
    std::pair<int, int> activeUsers();                     // {admins, guards}

private:
    Db& db_;
};

class UserRepository {
public:
    explicit UserRepository(Db& db) : db_(db) {}

    int count();
    int countActiveAdmins();
    int64_t create(const User& u);                       // username, fullName, passwordHash, role, active
    std::optional<User> get(int64_t id);
    std::optional<User> byUsername(const std::string& username);
    std::vector<User> list();
    bool update(const User& u);                          // fullName, role, active
    void setPassword(int64_t id, const std::string& passwordHash);
    bool remove(int64_t id);
    void touchLogin(int64_t id);

    void deleteSessionsFor(int64_t userId);              // signs the user out everywhere

    void createSession(const std::string& tokenHash, int64_t userId, int hours);
    std::optional<User> sessionUser(const std::string& tokenHash);   // unexpired, active users only
    void deleteSession(const std::string& tokenHash);
    void purgeExpiredSessions();

private:
    Db& db_;
};

} // namespace guard
