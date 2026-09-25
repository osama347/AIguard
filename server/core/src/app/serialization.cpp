#include "app/serialization.h"

using json = nlohmann::json;

namespace guard {

json isoTime(const std::string& t) {
    if (t.empty()) return nullptr;
    std::string s = t;
    if (s.size() >= 19 && s[10] == ' ') s[10] = 'T';
    return s + "Z";
}

json toJson(const Driver& d) {
    json vehicles = json::array();
    for (const auto& v : d.vehicles) vehicles.push_back({{"id", v.id}, {"plate_number", v.plateNumber}});
    return {{"id", d.id}, {"name", d.name}, {"status", d.status}, {"phone", d.phone}, {"notes", d.notes},
            {"photo_count", d.templateCount}, {"vehicles", vehicles},
            {"created_at", isoTime(d.createdAt)}, {"updated_at", isoTime(d.updatedAt)}};
}

json toJson(const Vehicle& v) {
    json drivers = json::array();
    for (const auto& d : v.drivers) drivers.push_back({{"id", d.id}, {"name", d.name}});
    return {{"id", v.id}, {"plate_number", v.plateNumber}, {"plate_normalized", v.plateNormalized},
            {"make", v.make}, {"model", v.model}, {"color", v.color}, {"status", v.status},
            {"drivers", drivers}, {"created_at", isoTime(v.createdAt)}, {"updated_at", isoTime(v.updatedAt)}};
}

json toJson(const Job& j, int queuePosition) {
    json r{{"id", j.id}, {"status", j.status}, {"original_name", j.originalName}, {"size_bytes", j.sizeBytes},
           {"attempts", j.attempts}, {"progress", j.progress},
           {"verdict", j.verdict.empty() ? json(nullptr) : json(j.verdict)},
           {"result", j.resultJson.empty() ? json(nullptr) : json::parse(j.resultJson, nullptr, false)},
           {"error", j.error.empty() ? json(nullptr) : json(j.error)},
           {"created_by", j.createdBy}, {"created_at", isoTime(j.createdAt)},
           {"started_at", isoTime(j.startedAt)}, {"finished_at", isoTime(j.finishedAt)}};
    if (queuePosition >= 0) r["queue_position"] = queuePosition;
    return r;
}

json toJson(const Camera& c) {
    return {{"id", c.id}, {"name", c.name}, {"source", c.source}, {"enabled", c.enabled},
            {"sample_fps", c.sampleFps}, {"created_at", isoTime(c.createdAt)}, {"updated_at", isoTime(c.updatedAt)}};
}

json toJson(const AccessEvent& e) {
    return {{"id", e.id}, {"event_time", isoTime(e.eventTime)}, {"ended_at", isoTime(e.endedAt)},
            {"source", e.fromCamera() ? "camera" : "video"},
            {"job_id", e.jobId.empty() ? json(nullptr) : json(e.jobId)},
            {"camera_id", e.cameraId ? json(*e.cameraId) : json(nullptr)},
            {"camera_name", e.cameraName.empty() ? json(nullptr) : json(e.cameraName)},
            {"has_snapshot", !e.snapshot.empty()},
            {"verdict", e.verdict}, {"authorized", e.authorized},
            {"driver_id", e.driverId ? json(*e.driverId) : json(nullptr)},
            {"driver_name", e.driverName.empty() ? json(nullptr) : json(e.driverName)},
            {"vehicle_id", e.vehicleId ? json(*e.vehicleId) : json(nullptr)},
            {"plate_text", e.plateText},
            {"details", e.detailsJson.empty() ? json::object() : json::parse(e.detailsJson, nullptr, false)}};
}

json toJson(const Alert& a) {
    return {{"id", a.id}, {"created_at", isoTime(a.createdAt)}, {"severity", a.severity}, {"kind", a.kind},
            {"message", a.message},
            {"access_event_id", a.accessEventId ? json(*a.accessEventId) : json(nullptr)},
            {"acknowledged_at", isoTime(a.acknowledgedAt)}, {"resolved_at", isoTime(a.resolvedAt)},
            {"acknowledged_by", a.acknowledgedBy.empty() ? json(nullptr) : json(a.acknowledgedBy)},
            {"resolved_by", a.resolvedBy.empty() ? json(nullptr) : json(a.resolvedBy)}};
}

json toJson(const User& u) {
    return {{"id", u.id}, {"username", u.username}, {"full_name", u.fullName}, {"role", u.role},
            {"active", u.active}, {"created_at", isoTime(u.createdAt)}, {"last_login_at", isoTime(u.lastLoginAt)}};
}

json toJson(const Decision& d, const Evidence& ev) {
    json driversSeen = json::array();
    for (const auto& [id, t] : ev.drivers())
        driversSeen.push_back({{"id", t.driverId}, {"name", t.name}, {"status", t.status},
                               {"frames", t.frames}, {"best_similarity", t.bestSimilarity}});
    json platesRead = json::array();
    for (const auto& [text, t] : ev.plates())
        platesRead.push_back({{"text", t.text}, {"reads", t.reads},
                              {"vehicle_id", t.vehicleId ? json(*t.vehicleId) : json(nullptr)}});

    json driver = nullptr, vehicle = nullptr;
    if (d.driver)
        driver = {{"id", d.driver->driverId}, {"name", d.driver->name},
                  {"similarity", d.driver->bestSimilarity}, {"frames", d.driver->frames}};
    if (d.vehicle)
        vehicle = {{"id", *d.vehicle->vehicleId}, {"plate_number", d.vehicle->text}, {"reads", d.vehicle->reads}};

    return {{"verdict", d.verdict}, {"authorized", d.authorized}, {"blacklisted", d.blacklisted},
            {"reason", d.reason}, {"driver", driver}, {"vehicle", vehicle},
            {"drivers_seen", driversSeen}, {"plates_read", platesRead},
            {"faces_seen", ev.facesSeen()}, {"unknown_faces", ev.unknownFaces()},
            {"frames_analysed", ev.frames()}};
}

} // namespace guard
