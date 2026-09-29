#include "infra/repositories.h"

namespace guard {

// ======================================================================
// CameraRepository
// ======================================================================

namespace {

const char* kCameraCols = "id, name, source, enabled, sample_fps, created_at, updated_at";

Camera readCamera(Stmt& st) {
    Camera c;
    c.id = st.i64(0);
    c.name = st.text(1);
    c.source = st.text(2);
    c.enabled = st.i64(3) != 0;
    c.sampleFps = st.f64(4);
    c.createdAt = st.text(5);
    c.updatedAt = st.text(6);
    return c;
}

} // namespace

std::vector<Camera> CameraRepository::list() {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kCameraCols + " FROM cameras ORDER BY name");
    std::vector<Camera> out;
    while (st.step()) out.push_back(readCamera(st));
    return out;
}

std::optional<Camera> CameraRepository::get(int64_t id) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kCameraCols + " FROM cameras WHERE id = ?");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return readCamera(st);
}

int64_t CameraRepository::create(const Camera& c) {
    auto lk = db_.lock();
    Stmt st(db_, "INSERT INTO cameras(name, source, enabled, sample_fps) VALUES(?, ?, ?, ?)");
    st.bind(1, c.name).bind(2, c.source).bind(3, c.enabled ? 1 : 0).bind(4, c.sampleFps).run();
    return db_.lastInsertId();
}

bool CameraRepository::update(const Camera& c) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE cameras SET name = ?, source = ?, enabled = ?, sample_fps = ?, "
                 "updated_at = datetime('now') WHERE id = ?");
    st.bind(1, c.name).bind(2, c.source).bind(3, c.enabled ? 1 : 0).bind(4, c.sampleFps).bind(5, c.id).run();
    return db_.changes() > 0;
}

bool CameraRepository::remove(int64_t id) {
    auto lk = db_.lock();
    Stmt st(db_, "DELETE FROM cameras WHERE id = ?");
    st.bind(1, id).run();
    return db_.changes() > 0;
}

Community CommunityRepository::get() {
    auto lk = db_.lock();
    Stmt st(db_, "SELECT name, address, city, country, helpline, email, website, logo_file, updated_at "
                 "FROM community WHERE id = 1");
    Community c;
    if (st.step()) {
        c.name = st.text(0); c.address = st.text(1); c.city = st.text(2); c.country = st.text(3);
        c.helpline = st.text(4); c.email = st.text(5); c.website = st.text(6);
        c.logoFile = st.text(7); c.updatedAt = st.text(8);
    }
    return c;
}

void CommunityRepository::save(const Community& c) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE community SET name = ?, address = ?, city = ?, country = ?, helpline = ?, "
                 "email = ?, website = ?, logo_file = ?, updated_at = datetime('now') WHERE id = 1");
    st.bind(1, c.name).bind(2, c.address).bind(3, c.city).bind(4, c.country).bind(5, c.helpline)
      .bind(6, c.email).bind(7, c.website).bind(8, c.logoFile).run();
}

// ======================================================================
// FleetRepository
// ======================================================================

namespace {

const char* kDriverCols =
    "d.id, d.name, d.status, d.phone, d.notes, d.is_owner, d.created_at, d.updated_at, "
    "(SELECT COUNT(*) FROM face_templates t WHERE t.driver_id = d.id)";

Driver readDriver(Stmt& st) {
    Driver d;
    d.id = st.i64(0);
    d.name = st.text(1);
    d.status = st.text(2);
    d.phone = st.text(3);
    d.notes = st.text(4);
    d.isOwner = st.i64(5) != 0;
    d.createdAt = st.text(6);
    d.updatedAt = st.text(7);
    d.templateCount = static_cast<int>(st.i64(8));
    return d;
}

const char* kVehicleCols =
    "v.id, v.plate_number, v.plate_normalized, v.make, v.model, v.color, v.status, "
    "v.created_at, v.updated_at, v.auth_code, o.id, o.name, o.phone";
const char* kVehicleFrom = "FROM vehicles v LEFT JOIN drivers o ON o.id = v.owner_id";

Vehicle readVehicle(Stmt& st) {
    Vehicle v;
    v.id = st.i64(0);
    v.plateNumber = st.text(1);
    v.plateNormalized = st.text(2);
    v.make = st.text(3);
    v.model = st.text(4);
    v.color = st.text(5);
    v.status = st.text(6);
    v.createdAt = st.text(7);
    v.updatedAt = st.text(8);
    if (!st.isNull(9)) v.authCode = st.text(9);
    if (!st.isNull(10)) v.owner = OwnerRef{st.i64(10), st.text(11), st.text(12)};
    return v;
}

} // namespace

void FleetRepository::loadDriverVehicles(Driver& d) {
    Stmt st(db_, "SELECT v.id, v.plate_number FROM assignments a JOIN vehicles v ON v.id = a.vehicle_id "
                 "WHERE a.driver_id = ? ORDER BY v.plate_number");
    st.bind(1, d.id);
    while (st.step()) d.vehicles.push_back({st.i64(0), st.text(1)});
}

void FleetRepository::loadVehicleDrivers(Vehicle& v) {
    Stmt st(db_, "SELECT d.id, d.name FROM assignments a JOIN drivers d ON d.id = a.driver_id "
                 "WHERE a.vehicle_id = ? ORDER BY d.name");
    st.bind(1, v.id);
    while (st.step()) v.drivers.push_back({st.i64(0), st.text(1)});
}

std::vector<Driver> FleetRepository::listDrivers() {
    auto lk = db_.lock();
    std::vector<Driver> out;
    Stmt st(db_, std::string("SELECT ") + kDriverCols + " FROM drivers d ORDER BY d.name COLLATE NOCASE");
    while (st.step()) out.push_back(readDriver(st));
    for (auto& d : out) loadDriverVehicles(d);
    return out;
}

std::optional<Driver> FleetRepository::getDriver(int64_t id) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kDriverCols + " FROM drivers d WHERE d.id = ?");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    Driver d = readDriver(st);
    loadDriverVehicles(d);
    return d;
}

int64_t FleetRepository::createDriver(const Driver& d) {
    auto lk = db_.lock();
    Stmt st(db_, "INSERT INTO drivers(name, status, phone, notes, is_owner) VALUES(?, ?, ?, ?, ?)");
    st.bind(1, d.name).bind(2, d.status).bind(3, d.phone).bind(4, d.notes).bind(5, d.isOwner ? 1 : 0).run();
    return db_.lastInsertId();
}

bool FleetRepository::updateDriver(const Driver& d) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE drivers SET name = ?, status = ?, phone = ?, notes = ?, "
                 "updated_at = datetime('now') WHERE id = ?");
    st.bind(1, d.name).bind(2, d.status).bind(3, d.phone).bind(4, d.notes).bind(5, d.id).run();
    return db_.changes() > 0;
}

bool FleetRepository::deleteDriver(int64_t id) {
    auto lk = db_.lock();
    Stmt st(db_, "DELETE FROM drivers WHERE id = ?");
    st.bind(1, id).run();
    return db_.changes() > 0;
}

void FleetRepository::addTemplates(int64_t driverId, const std::vector<std::vector<float>>& embeddings,
                                   const std::string& modelVersion) {
    db_.transaction([&] {
        for (const auto& e : embeddings) {
            Stmt st(db_, "INSERT INTO face_templates(driver_id, embedding, model_version) VALUES(?, ?, ?)");
            st.bind(1, driverId).bindBlob(2, e).bind(3, modelVersion).run();
        }
        Stmt touch(db_, "UPDATE drivers SET updated_at = datetime('now') WHERE id = ?");
        touch.bind(1, driverId).run();
    });
}

int FleetRepository::clearTemplates(int64_t driverId) {
    auto lk = db_.lock();
    Stmt st(db_, "DELETE FROM face_templates WHERE driver_id = ?");
    st.bind(1, driverId).run();
    return db_.changes();
}

std::vector<Identity> FleetRepository::identities(const std::string& modelVersion) {
    auto lk = db_.lock();
    std::vector<Identity> out;
    Stmt st(db_, "SELECT d.id, d.name, d.status, t.embedding FROM face_templates t "
                 "JOIN drivers d ON d.id = t.driver_id WHERE t.model_version = ? ORDER BY d.id");
    st.bind(1, modelVersion);
    while (st.step()) {
        const int64_t id = st.i64(0);
        if (out.empty() || out.back().driverId != id) out.push_back({id, st.text(1), st.text(2), {}});
        out.back().templates.push_back(st.floats(3));
    }
    return out;
}

std::vector<Vehicle> FleetRepository::listVehicles() {
    auto lk = db_.lock();
    std::vector<Vehicle> out;
    Stmt st(db_, std::string("SELECT ") + kVehicleCols + " " + kVehicleFrom + " ORDER BY v.plate_normalized");
    while (st.step()) out.push_back(readVehicle(st));
    for (auto& v : out) loadVehicleDrivers(v);
    return out;
}

std::optional<Vehicle> FleetRepository::getVehicle(int64_t id) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kVehicleCols + " " + kVehicleFrom + " WHERE v.id = ?");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    Vehicle v = readVehicle(st);
    loadVehicleDrivers(v);
    return v;
}

std::optional<Vehicle> FleetRepository::vehicleByPlate(const std::string& normalized) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kVehicleCols + " " + kVehicleFrom + " WHERE v.plate_normalized = ?");
    st.bind(1, normalized);
    if (!st.step()) return std::nullopt;
    return readVehicle(st);
}

std::optional<Vehicle> FleetRepository::vehicleByAuthCode(const std::string& code) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kVehicleCols + " " + kVehicleFrom + " WHERE v.auth_code = ?");
    st.bind(1, code);
    if (!st.step()) return std::nullopt;
    Vehicle v = readVehicle(st);
    loadVehicleDrivers(v);
    return v;
}

int64_t FleetRepository::createVehicle(const Vehicle& v) {
    auto lk = db_.lock();
    Stmt st(db_, "INSERT INTO vehicles(plate_number, plate_normalized, make, model, color, status) "
                 "VALUES(?, ?, ?, ?, ?, ?)");
    st.bind(1, v.plateNumber).bind(2, v.plateNormalized).bind(3, v.make).bind(4, v.model)
      .bind(5, v.color).bind(6, v.status).run();
    return db_.lastInsertId();
}

bool FleetRepository::updateVehicle(const Vehicle& v) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE vehicles SET plate_number = ?, plate_normalized = ?, make = ?, model = ?, "
                 "color = ?, status = ?, updated_at = datetime('now') WHERE id = ?");
    st.bind(1, v.plateNumber).bind(2, v.plateNormalized).bind(3, v.make).bind(4, v.model)
      .bind(5, v.color).bind(6, v.status).bind(7, v.id).run();
    return db_.changes() > 0;
}

bool FleetRepository::deleteVehicle(int64_t id) {
    auto lk = db_.lock();
    Stmt st(db_, "DELETE FROM vehicles WHERE id = ?");
    st.bind(1, id).run();
    return db_.changes() > 0;
}

bool FleetRepository::assign(int64_t driverId, int64_t vehicleId) {
    auto lk = db_.lock();
    Stmt st(db_, "INSERT OR IGNORE INTO assignments(driver_id, vehicle_id) VALUES(?, ?)");
    st.bind(1, driverId).bind(2, vehicleId).run();
    return true;
}

bool FleetRepository::unassign(int64_t driverId, int64_t vehicleId) {
    auto lk = db_.lock();
    Stmt st(db_, "DELETE FROM assignments WHERE driver_id = ? AND vehicle_id = ?");
    st.bind(1, driverId).bind(2, vehicleId).run();
    return db_.changes() > 0;
}

AssignmentSet FleetRepository::assignments() {
    auto lk = db_.lock();
    AssignmentSet out;
    Stmt st(db_, "SELECT driver_id, vehicle_id FROM assignments");
    while (st.step()) out.insert({st.i64(0), st.i64(1)});
    return out;
}

int64_t FleetRepository::createVehicleWithOwner(const Vehicle& v, const Driver& owner, const std::string& code) {
    auto lk = db_.lock();
    int64_t vehicleId = 0;
    db_.transaction([&] {
        vehicleId = createVehicle(v);
        const int64_t ownerId = createDriver(owner);
        assign(ownerId, vehicleId);
        Stmt st(db_, "UPDATE vehicles SET owner_id = ?, auth_code = ? WHERE id = ?");
        st.bind(1, ownerId).bind(2, code).bind(3, vehicleId).run();
    });
    return vehicleId;
}

void FleetRepository::setVehicleOwner(int64_t vehicleId, const Driver& owner, const std::string& code) {
    auto lk = db_.lock();
    db_.transaction([&] {
        const int64_t ownerId = createDriver(owner);
        assign(ownerId, vehicleId);
        Stmt st(db_, "UPDATE vehicles SET owner_id = ?, auth_code = ? WHERE id = ?");
        st.bind(1, ownerId).bind(2, code).bind(3, vehicleId).run();
    });
}

// ======================================================================
// JobRepository
// ======================================================================

namespace {

const char* kJobCols =
    "id, status, source, original_name, size_bytes, attempts, progress, COALESCE(verdict, ''), "
    "COALESCE(result, ''), COALESCE(error, ''), created_by, created_at, COALESCE(started_at, ''), "
    "COALESCE(finished_at, '')";

Job readJob(Stmt& st) {
    Job j;
    j.id = st.text(0);
    j.status = st.text(1);
    j.source = st.text(2);
    j.originalName = st.text(3);
    j.sizeBytes = st.i64(4);
    j.attempts = static_cast<int>(st.i64(5));
    j.progress = st.f64(6);
    j.verdict = st.text(7);
    j.resultJson = st.text(8);
    j.error = st.text(9);
    j.createdBy = st.text(10);
    j.createdAt = st.text(11);
    j.startedAt = st.text(12);
    j.finishedAt = st.text(13);
    return j;
}

} // namespace

void JobRepository::create(const Job& j) {
    auto lk = db_.lock();
    Stmt st(db_, "INSERT INTO jobs(id, status, source, original_name, size_bytes, created_by) "
                 "VALUES(?, 'queued', ?, ?, ?, ?)");
    st.bind(1, j.id).bind(2, j.source).bind(3, j.originalName).bind(4, j.sizeBytes).bind(5, j.createdBy).run();
}

std::optional<Job> JobRepository::get(const std::string& id) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kJobCols + " FROM jobs WHERE id = ?");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return readJob(st);
}

std::vector<Job> JobRepository::list(int limit, int offset) {
    auto lk = db_.lock();
    std::vector<Job> out;
    Stmt st(db_, std::string("SELECT ") + kJobCols + " FROM jobs ORDER BY created_at DESC, rowid DESC LIMIT ? OFFSET ?");
    st.bind(1, limit).bind(2, offset);
    while (st.step()) out.push_back(readJob(st));
    return out;
}

std::optional<Job> JobRepository::nextQueued() {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kJobCols +
                     " FROM jobs WHERE status = 'queued' ORDER BY created_at, rowid LIMIT 1");
    if (!st.step()) return std::nullopt;
    return readJob(st);
}

int JobRepository::queuedAhead(const std::string& id) {
    auto lk = db_.lock();
    Stmt st(db_, "SELECT COUNT(*) FROM jobs WHERE status IN ('queued', 'running') AND rowid < "
                 "(SELECT rowid FROM jobs WHERE id = ?)");
    st.bind(1, id);
    st.step();
    return static_cast<int>(st.i64(0));
}

void JobRepository::markRunning(const std::string& id) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE jobs SET status = 'running', attempts = attempts + 1, progress = 0, "
                 "started_at = datetime('now') WHERE id = ?");
    st.bind(1, id).run();
}

void JobRepository::setProgress(const std::string& id, double progress) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE jobs SET progress = ? WHERE id = ?");
    st.bind(1, progress).bind(2, id).run();
}

void JobRepository::complete(const std::string& id, const std::string& verdict, const std::string& resultJson) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE jobs SET status = 'completed', progress = 1, verdict = ?, result = ?, error = NULL, "
                 "finished_at = datetime('now') WHERE id = ?");
    st.bind(1, verdict).bind(2, resultJson).bind(3, id).run();
}

void JobRepository::fail(const std::string& id, const std::string& error) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE jobs SET status = 'failed', error = ?, finished_at = datetime('now') WHERE id = ?");
    st.bind(1, error).bind(2, id).run();
}

void JobRepository::cancel(const std::string& id) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE jobs SET status = 'cancelled', finished_at = datetime('now') WHERE id = ?");
    st.bind(1, id).run();
}

void JobRepository::requeue(const std::string& id) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE jobs SET status = 'queued' WHERE id = ?");
    st.bind(1, id).run();
}

bool JobRepository::remove(const std::string& id) {
    auto lk = db_.lock();
    Stmt st(db_, "DELETE FROM jobs WHERE id = ?");
    st.bind(1, id).run();
    return db_.changes() > 0;
}

int JobRepository::requeueInterrupted() {
    auto lk = db_.lock();
    db_.exec("UPDATE jobs SET status = 'queued' WHERE status = 'running'");
    return db_.changes();
}

// ======================================================================
// EventRepository
// ======================================================================

namespace {

const char* kAccessEventSelect =
    "SELECT e.id, e.event_time, COALESCE(e.job_id, ''), e.verdict, e.authorized, e.driver_id, "
    "e.vehicle_id, COALESCE(d.name, ''), e.plate_text, e.details, e.camera_id, COALESCE(c.name, ''), "
    "COALESCE(e.ended_at, ''), e.snapshot FROM access_events e "
    "LEFT JOIN drivers d ON d.id = e.driver_id LEFT JOIN cameras c ON c.id = e.camera_id ";

AccessEvent readAccessEvent(Stmt& st) {
    AccessEvent e;
    e.id = st.i64(0);
    e.eventTime = st.text(1);
    e.jobId = st.text(2);
    e.verdict = st.text(3);
    e.authorized = st.i64(4) != 0;
    e.driverId = st.optI64(5);
    e.vehicleId = st.optI64(6);
    e.driverName = st.text(7);
    e.plateText = st.text(8);
    e.detailsJson = st.text(9);
    e.cameraId = st.optI64(10);
    e.cameraName = st.text(11);
    e.endedAt = st.text(12);
    e.snapshot = st.text(13);
    return e;
}

} // namespace

int64_t EventRepository::insertAccessEvent(const AccessEvent& e) {
    auto lk = db_.lock();
    Stmt st(db_, "INSERT INTO access_events(event_time, job_id, verdict, authorized, driver_id, vehicle_id, "
                 "plate_text, details, camera_id, ended_at, snapshot) "
                 "VALUES(COALESCE(?, datetime('now')), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (e.eventTime.empty()) st.bindNull(1); else st.bind(1, e.eventTime);
    if (e.jobId.empty()) st.bindNull(2); else st.bind(2, e.jobId);
    st.bind(3, e.verdict).bind(4, e.authorized ? 1 : 0).bind(5, e.driverId).bind(6, e.vehicleId)
      .bind(7, e.plateText).bind(8, e.detailsJson).bind(9, e.cameraId);
    if (e.endedAt.empty()) st.bindNull(10); else st.bind(10, e.endedAt);
    st.bind(11, e.snapshot).run();
    return db_.lastInsertId();
}

std::optional<AccessEvent> EventRepository::getAccessEvent(int64_t id) {
    auto lk = db_.lock();
    Stmt st(db_, std::string(kAccessEventSelect) + "WHERE e.id = ?");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return readAccessEvent(st);
}

std::vector<AccessEvent> EventRepository::listAccessEvents(int limit, int offset, const AccessEventFilter& f) {
    auto lk = db_.lock();
    std::string where = "WHERE 1 = 1 ";
    if (f.cameraId) where += "AND e.camera_id = ? ";
    if (f.source == "camera") where += "AND e.ended_at IS NOT NULL ";   // see AccessEvent::fromCamera
    if (f.source == "video") where += "AND e.ended_at IS NULL ";
    if (!f.verdict.empty()) where += "AND e.verdict = ? ";
    Stmt st(db_, std::string(kAccessEventSelect) + where + "ORDER BY e.event_time DESC, e.id DESC LIMIT ? OFFSET ?");
    int i = 1;
    if (f.cameraId) st.bind(i++, *f.cameraId);
    if (!f.verdict.empty()) st.bind(i++, f.verdict);
    st.bind(i, limit);
    st.bind(i + 1, offset);
    std::vector<AccessEvent> out;
    while (st.step()) out.push_back(readAccessEvent(st));
    return out;
}

void EventRepository::extendAccessEvent(int64_t id, const std::string& endedAt) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE access_events SET ended_at = ? WHERE id = ?");
    st.bind(1, endedAt).bind(2, id).run();
}

int EventRepository::clearSnapshots(const std::string& pathPrefix) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE access_events SET snapshot = '' WHERE substr(snapshot, 1, ?) = ?");
    st.bind(1, static_cast<int64_t>(pathPrefix.size())).bind(2, pathPrefix).run();
    return db_.changes();
}

namespace {

const char* kAlertCols =
    "id, created_at, severity, kind, message, access_event_id, COALESCE(acknowledged_at, ''), "
    "COALESCE(resolved_at, ''), acknowledged_by, resolved_by";

Alert readAlert(Stmt& st) {
    Alert a;
    a.id = st.i64(0);
    a.createdAt = st.text(1);
    a.severity = st.text(2);
    a.kind = st.text(3);
    a.message = st.text(4);
    a.accessEventId = st.optI64(5);
    a.acknowledgedAt = st.text(6);
    a.resolvedAt = st.text(7);
    a.acknowledgedBy = st.text(8);
    a.resolvedBy = st.text(9);
    return a;
}

} // namespace

Alert EventRepository::insertAlert(const std::string& severity, const std::string& kind,
                                   const std::string& message, std::optional<int64_t> accessEventId) {
    auto lk = db_.lock();
    Stmt st(db_, "INSERT INTO alerts(severity, kind, message, access_event_id) VALUES(?, ?, ?, ?)");
    st.bind(1, severity).bind(2, kind).bind(3, message).bind(4, accessEventId).run();
    return *getAlert(db_.lastInsertId());
}

std::optional<Alert> EventRepository::getAlert(int64_t id) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kAlertCols + " FROM alerts WHERE id = ?");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return readAlert(st);
}

std::vector<Alert> EventRepository::listAlerts(bool openOnly, int limit) {
    auto lk = db_.lock();
    std::vector<Alert> out;
    Stmt st(db_, std::string("SELECT ") + kAlertCols + " FROM alerts " +
                     (openOnly ? "WHERE resolved_at IS NULL " : "") + "ORDER BY id DESC LIMIT ?");
    st.bind(1, limit);
    while (st.step()) out.push_back(readAlert(st));
    return out;
}

bool EventRepository::acknowledge(int64_t id, const std::string& by) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE alerts SET acknowledged_by = CASE WHEN acknowledged_at IS NULL THEN ? ELSE acknowledged_by END, "
                 "acknowledged_at = COALESCE(acknowledged_at, datetime('now')) WHERE id = ?");
    st.bind(1, by).bind(2, id).run();
    return db_.changes() > 0;
}

bool EventRepository::resolve(int64_t id, const std::string& by) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE alerts SET "
                 "acknowledged_by = CASE WHEN acknowledged_at IS NULL THEN ? ELSE acknowledged_by END, "
                 "acknowledged_at = COALESCE(acknowledged_at, datetime('now')), "
                 "resolved_by = CASE WHEN resolved_at IS NULL THEN ? ELSE resolved_by END, "
                 "resolved_at = COALESCE(resolved_at, datetime('now')) WHERE id = ?");
    st.bind(1, by).bind(2, by).bind(3, id).run();
    return db_.changes() > 0;
}

void EventRepository::audit(const std::string& actor, const std::string& action, const std::string& entity,
                            const std::string& entityId, const std::string& details) {
    auto lk = db_.lock();
    Stmt st(db_, "INSERT INTO audit_log(actor, action, entity, entity_id, details) VALUES(?, ?, ?, ?, ?)");
    st.bind(1, actor).bind(2, action).bind(3, entity).bind(4, entityId).bind(5, details).run();
}

// ======================================================================
// UserRepository
// ======================================================================

namespace {

const char* kUserCols = "u.id, u.username, u.password_hash, u.role, u.created_at, u.full_name, u.active, "
                        "COALESCE(u.last_login_at, '')";

User readUser(Stmt& st) {
    User u;
    u.id = st.i64(0);
    u.username = st.text(1);
    u.passwordHash = st.text(2);
    u.role = st.text(3);
    u.createdAt = st.text(4);
    u.fullName = st.text(5);
    u.active = st.i64(6) != 0;
    u.lastLoginAt = st.text(7);
    return u;
}

} // namespace

int UserRepository::count() {
    auto lk = db_.lock();
    Stmt st(db_, "SELECT COUNT(*) FROM users");
    st.step();
    return static_cast<int>(st.i64(0));
}

int UserRepository::countActiveAdmins() {
    auto lk = db_.lock();
    Stmt st(db_, "SELECT COUNT(*) FROM users WHERE role = 'admin' AND active = 1");
    st.step();
    return static_cast<int>(st.i64(0));
}

int64_t UserRepository::create(const User& u) {
    auto lk = db_.lock();
    Stmt st(db_, "INSERT INTO users(username, full_name, password_hash, role, active) VALUES(?, ?, ?, ?, ?)");
    st.bind(1, u.username).bind(2, u.fullName).bind(3, u.passwordHash).bind(4, u.role).bind(5, u.active ? 1 : 0).run();
    return db_.lastInsertId();
}

std::optional<User> UserRepository::get(int64_t id) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kUserCols + " FROM users u WHERE u.id = ?");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return readUser(st);
}

std::optional<User> UserRepository::byUsername(const std::string& username) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kUserCols + " FROM users u WHERE u.username = ?");
    st.bind(1, username);
    if (!st.step()) return std::nullopt;
    return readUser(st);
}

std::vector<User> UserRepository::list() {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kUserCols + " FROM users u ORDER BY u.role, u.username");
    std::vector<User> out;
    while (st.step()) out.push_back(readUser(st));
    return out;
}

bool UserRepository::update(const User& u) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE users SET full_name = ?, role = ?, active = ? WHERE id = ?");
    st.bind(1, u.fullName).bind(2, u.role).bind(3, u.active ? 1 : 0).bind(4, u.id).run();
    return db_.changes() > 0;
}

void UserRepository::setPassword(int64_t id, const std::string& passwordHash) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE users SET password_hash = ? WHERE id = ?");
    st.bind(1, passwordHash).bind(2, id).run();
}

bool UserRepository::remove(int64_t id) {
    auto lk = db_.lock();
    Stmt st(db_, "DELETE FROM users WHERE id = ?");
    st.bind(1, id).run();
    return db_.changes() > 0;
}

void UserRepository::touchLogin(int64_t id) {
    auto lk = db_.lock();
    Stmt st(db_, "UPDATE users SET last_login_at = datetime('now') WHERE id = ?");
    st.bind(1, id).run();
}

void UserRepository::deleteSessionsFor(int64_t userId) {
    auto lk = db_.lock();
    Stmt st(db_, "DELETE FROM sessions WHERE user_id = ?");
    st.bind(1, userId).run();
}

void UserRepository::createSession(const std::string& tokenHash, int64_t userId, int hours) {
    auto lk = db_.lock();
    Stmt st(db_, "INSERT INTO sessions(token_hash, user_id, expires_at) VALUES(?, ?, datetime('now', ?))");
    const std::string modifier = (hours >= 0 ? "+" : "") + std::to_string(hours) + " hours";
    st.bind(1, tokenHash).bind(2, userId).bind(3, modifier).run();
}

std::optional<User> UserRepository::sessionUser(const std::string& tokenHash) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT ") + kUserCols + " FROM sessions s JOIN users u ON u.id = s.user_id "
                 "WHERE s.token_hash = ? AND s.expires_at > datetime('now') AND u.active = 1");
    st.bind(1, tokenHash);
    if (!st.step()) return std::nullopt;
    return readUser(st);
}

void UserRepository::deleteSession(const std::string& tokenHash) {
    auto lk = db_.lock();
    Stmt st(db_, "DELETE FROM sessions WHERE token_hash = ?");
    st.bind(1, tokenHash).run();
}

void UserRepository::purgeExpiredSessions() {
    db_.exec("DELETE FROM sessions WHERE expires_at <= datetime('now')");
}

// ======================================================================
// StatsRepository
// ======================================================================

namespace {
// A camera visit (see AccessEvent::fromCamera).
const char* kCameraVisit = "e.ended_at IS NOT NULL";
}

std::vector<VerdictBucket> StatsRepository::hourly(int hours) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT strftime('%Y-%m-%d %H:00', e.event_time, 'localtime') AS b, e.verdict, COUNT(*) "
                             "FROM access_events e WHERE ") + kCameraVisit +
                 " AND e.event_time >= datetime('now', ?) GROUP BY b, e.verdict ORDER BY b");
    st.bind(1, "-" + std::to_string(hours) + " hours");
    std::vector<VerdictBucket> out;
    while (st.step()) out.push_back({st.text(0), st.text(1), static_cast<int>(st.i64(2))});
    return out;
}

std::vector<VerdictBucket> StatsRepository::daily(int days) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT date(e.event_time, 'localtime') AS b, e.verdict, COUNT(*) FROM access_events e WHERE ") +
                 kCameraVisit + " AND e.event_time >= datetime('now', 'localtime', 'start of day', ?, 'utc') "
                 "GROUP BY b, e.verdict ORDER BY b");
    st.bind(1, "-" + std::to_string(days - 1) + " days");
    std::vector<VerdictBucket> out;
    while (st.step()) out.push_back({st.text(0), st.text(1), static_cast<int>(st.i64(2))});
    return out;
}

std::vector<std::pair<int64_t, int>> StatsRepository::visitsTodayByCamera() {
    auto lk = db_.lock();
    Stmt st(db_, "SELECT e.camera_id, COUNT(*) FROM access_events e WHERE e.camera_id IS NOT NULL "
                 "AND e.event_time >= datetime('now', 'localtime', 'start of day', 'utc') GROUP BY e.camera_id");
    std::vector<std::pair<int64_t, int>> out;
    while (st.step()) out.emplace_back(st.i64(0), static_cast<int>(st.i64(1)));
    return out;
}

std::vector<PlateCount> StatsRepository::unregisteredPlates(int days, int limit) {
    auto lk = db_.lock();
    // Plates registered since they were seen drop off the list.
    Stmt st(db_, std::string("SELECT e.plate_text, COUNT(*), MAX(e.event_time), "
                             "(SELECT c.name FROM access_events x LEFT JOIN cameras c ON c.id = x.camera_id "
                             " WHERE x.plate_text = e.plate_text ORDER BY x.event_time DESC LIMIT 1) "
                             "FROM access_events e WHERE ") + kCameraVisit +
                 " AND e.vehicle_id IS NULL AND e.plate_text != '' AND e.event_time >= datetime('now', ?) "
                 "AND e.plate_text NOT IN (SELECT plate_normalized FROM vehicles) "
                 "GROUP BY e.plate_text ORDER BY COUNT(*) DESC, MAX(e.event_time) DESC LIMIT ?");
    st.bind(1, "-" + std::to_string(days) + " days").bind(2, limit);
    std::vector<PlateCount> out;
    while (st.step()) out.push_back({st.text(0), static_cast<int>(st.i64(1)), st.text(2), st.text(3)});
    return out;
}

std::vector<DriverCount> StatsRepository::topDrivers(int days, int limit) {
    auto lk = db_.lock();
    Stmt st(db_, std::string("SELECT d.id, d.name, COUNT(*), SUM(e.authorized) FROM access_events e "
                             "JOIN drivers d ON d.id = e.driver_id WHERE ") + kCameraVisit +
                 " AND e.event_time >= datetime('now', ?) GROUP BY d.id ORDER BY COUNT(*) DESC LIMIT ?");
    st.bind(1, "-" + std::to_string(days) + " days").bind(2, limit);
    std::vector<DriverCount> out;
    while (st.step()) out.push_back({st.i64(0), st.text(1), static_cast<int>(st.i64(2)), static_cast<int>(st.i64(3))});
    return out;
}

AlertStats StatsRepository::alerts() {
    auto lk = db_.lock();
    AlertStats a;
    Stmt counts(db_, "SELECT COUNT(*), COALESCE(SUM(severity = 'critical'), 0), COALESCE(SUM(acknowledged_at IS NULL), 0) "
                     "FROM alerts WHERE resolved_at IS NULL");
    counts.step();
    a.open = static_cast<int>(counts.i64(0));
    a.openCritical = static_cast<int>(counts.i64(1));
    a.unacknowledged = static_cast<int>(counts.i64(2));
    Stmt resp(db_, "SELECT (julianday(acknowledged_at) - julianday(created_at)) * 86400 FROM alerts "
                   "WHERE acknowledged_at IS NOT NULL AND acknowledged_by NOT IN ('', 'system') "
                   "AND created_at >= datetime('now', '-7 days')");
    while (resp.step()) a.responseSeconds.push_back(resp.f64(0));
    return a;
}

std::pair<int, int> StatsRepository::activeUsers() {
    auto lk = db_.lock();
    Stmt st(db_, "SELECT COALESCE(SUM(role = 'admin'), 0), COALESCE(SUM(role = 'guard'), 0) FROM users WHERE active = 1");
    st.step();
    return {static_cast<int>(st.i64(0)), static_cast<int>(st.i64(1))};
}

} // namespace guard
