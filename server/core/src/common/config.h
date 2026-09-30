#pragma once
#include <cstddef>
#include <string>

namespace guard {

// core.json. Relative paths resolve against the config file's directory.
// Environment overrides: GUARD_INFERENCE_SECRET, GUARD_DATA_DIR.
struct CoreConfig {
    struct Server {
        std::string host = "0.0.0.0";
        int port = 8090;
        int threads = 16;
        std::size_t maxUploadMb = 1024;
    } server;

    struct Paths {
        std::string dataDir = "data";          // guard.db + media/ live here
        std::string migrationsDir = "migrations";
        std::string frontendDir;               // built UI; empty = API only
    } paths;

    struct Inference {
        std::string url = "http://127.0.0.1:8081";
        std::string secret;
        double sampleFps = 5.0;
        int maxAttempts = 3;                   // per job, across inference restarts
    } inference;

    struct Policy {
        float matchThreshold = 0.55f;          // cosine similarity for a face match
        int minFaceFrames = 1;                 // frames a driver must be matched in
        int minPlateReads = 1;                 // OCR reads a plate needs to count
    } policy;

    struct Live {
        double visitGapS = 4;                  // nothing seen this long ends a visit
        double maxVisitS = 60;                 // longer visits are split
        int minVisitFrames = 2;                // fewer frames with detections = noise
        double repeatSuppressS = 30;           // same outcome again within this = same visit
        int snapshotRetentionDays = 30;        // event snapshots older than this are deleted
        int minFreeDiskMb = 2048;              // below this, oldest snapshots are deleted early
        double offlineAlertS = 30;             // camera down this long raises an alert
    } live;

    struct Auth {
        int sessionHours = 12;
    } auth;

    std::string logLevel = "info";

    std::string dbPath() const { return dataDir() + "/guard.db"; }
    std::string mediaDir() const { return dataDir() + "/media"; }
    std::string brandingDir() const { return mediaDir() + "/branding"; }
    std::string portraitsDir() const { return mediaDir() + "/portraits"; }
    std::string snapshotsDir() const { return mediaDir() + "/snapshots"; }
    std::string dataDir() const { return paths.dataDir; }
};

bool loadCoreConfig(const std::string& path, CoreConfig& cfg, std::string& err);

} // namespace guard
