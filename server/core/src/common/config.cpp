#include "common/config.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace guard {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::string resolve(const fs::path& base, const std::string& p) {
    if (p.empty() || fs::path(p).is_absolute()) return p;
    return (base / p).lexically_normal().string();
}

template <typename T> void read(const json& j, const char* key, T& out) {
    if (j.contains(key) && !j[key].is_null()) out = j[key].get<T>();
}

} // namespace

bool loadCoreConfig(const std::string& path, CoreConfig& cfg, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open config file: " + path; return false; }
    json doc;
    try {
        doc = json::parse(f);
        const fs::path base = fs::path(path).parent_path();
        if (doc.contains("server")) {
            const auto& s = doc["server"];
            read(s, "host", cfg.server.host);
            read(s, "port", cfg.server.port);
            read(s, "threads", cfg.server.threads);
            read(s, "max_upload_mb", cfg.server.maxUploadMb);
        }
        if (doc.contains("paths")) {
            const auto& p = doc["paths"];
            read(p, "data_dir", cfg.paths.dataDir);
            read(p, "migrations_dir", cfg.paths.migrationsDir);
            read(p, "frontend_dir", cfg.paths.frontendDir);
        }
        cfg.paths.dataDir = resolve(base, cfg.paths.dataDir);
        cfg.paths.migrationsDir = resolve(base, cfg.paths.migrationsDir);
        cfg.paths.frontendDir = resolve(base, cfg.paths.frontendDir);
        if (doc.contains("inference")) {
            const auto& i = doc["inference"];
            read(i, "url", cfg.inference.url);
            read(i, "secret", cfg.inference.secret);
            read(i, "sample_fps", cfg.inference.sampleFps);
            read(i, "max_attempts", cfg.inference.maxAttempts);
        }
        if (doc.contains("policy")) {
            const auto& p = doc["policy"];
            read(p, "match_threshold", cfg.policy.matchThreshold);
            read(p, "min_face_frames", cfg.policy.minFaceFrames);
            read(p, "min_plate_reads", cfg.policy.minPlateReads);
        }
        if (doc.contains("live")) {
            const auto& l = doc["live"];
            read(l, "visit_gap_s", cfg.live.visitGapS);
            read(l, "max_visit_s", cfg.live.maxVisitS);
            read(l, "min_visit_frames", cfg.live.minVisitFrames);
            read(l, "repeat_suppress_s", cfg.live.repeatSuppressS);
            read(l, "snapshot_retention_days", cfg.live.snapshotRetentionDays);
            read(l, "min_free_disk_mb", cfg.live.minFreeDiskMb);
            read(l, "offline_alert_s", cfg.live.offlineAlertS);
        }
        if (doc.contains("auth")) read(doc["auth"], "session_hours", cfg.auth.sessionHours);
        if (doc.contains("logging")) read(doc["logging"], "level", cfg.logLevel);
    } catch (const std::exception& e) {
        err = "config error in " + path + ": " + e.what();
        return false;
    }
    if (const char* s = std::getenv("GUARD_INFERENCE_SECRET")) cfg.inference.secret = s;
    if (const char* d = std::getenv("GUARD_DATA_DIR")) cfg.paths.dataDir = d;
    return true;
}

} // namespace guard
