// ---------------------------------------------------------------------------
// guard-core — the public Guard++ service: REST + SSE API, business rules,
// embedded SQLite, live camera supervision, and the test-video job runner,
// all driving guard-inference.
//
//   guard-core [config/core.json]
// ---------------------------------------------------------------------------
#include "api/http_api.h"
#include "app/auth_service.h"
#include "app/camera_service.h"
#include "app/event_bus.h"
#include "app/fleet_service.h"
#include "app/job_service.h"
#include "common/config.h"
#include "common/log.h"
#include "infra/inference_client.h"
#include "infra/repositories.h"
#include "infra/sqlite_db.h"
#include <httplib.h>
#include <csignal>
#include <filesystem>

namespace {
httplib::Server* gServer = nullptr;
void onSignal(int) { if (gServer) gServer->stop(); }
}

int main(int argc, char** argv) {
    using namespace guard;
    const std::string configPath = argc > 1 ? argv[1] : "config/core.json";

    CoreConfig cfg;
    std::string err;
    if (!loadCoreConfig(configPath, cfg, err)) {
        LOG(Error) << err;
        return 1;
    }
    setLogLevel(cfg.logLevel);
    if (cfg.inference.secret.empty()) LOG(Warn) << "No inference secret configured";

    try {
        std::filesystem::create_directories(cfg.mediaDir());
        Db db(cfg.dbPath());
        db.migrate(cfg.paths.migrationsDir);
        LOG(Info) << "Database ready: " << cfg.dbPath() << " (schema v" << db.schemaVersion() << ")";

        FleetRepository fleetRepo(db);
        JobRepository jobRepo(db);
        EventRepository eventRepo(db);
        UserRepository userRepo(db);
        CameraRepository cameraRepo(db);
        CommunityRepository communityRepo(db);
        StatsRepository statsRepo(db);

        InferenceClient inference(cfg.inference.url, cfg.inference.secret);
        EventBus bus;
        FleetService fleet(fleetRepo, eventRepo, inference, cfg.policy.matchThreshold);
        AuthService auth(userRepo, eventRepo, cfg.auth.sessionHours);
        JobService jobs(jobRepo, fleet, eventRepo, inference, bus, cfg);
        CameraService cameras(cameraRepo, fleet, eventRepo, inference, bus, cfg);

        httplib::Server svr;
        svr.new_task_queue = [&cfg] { return new httplib::ThreadPool(std::max(8, cfg.server.threads)); };
        svr.set_payload_max_length((cfg.server.maxUploadMb + 1) << 20);
        svr.set_read_timeout(300, 0);
        svr.set_write_timeout(300, 0);
        registerApi(svr, ApiDeps{cfg, auth, fleet, jobs, cameras, communityRepo, eventRepo, statsRepo, inference, bus});

        jobs.start();
        cameras.start();
        gServer = &svr;
        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        if (auth.setupRequired()) LOG(Info) << "No users yet: create the admin account in the Guard++ app (from another PC, with the setup code from /etc/guard/guard.env)";
        LOG(Info) << "guard-core listening on " << cfg.server.host << ":" << cfg.server.port;
        if (!svr.listen(cfg.server.host, cfg.server.port)) {
            LOG(Error) << "cannot listen on " << cfg.server.host << ":" << cfg.server.port;
            return 1;
        }
        cameras.stop();
        jobs.stop();
        LOG(Info) << "guard-core stopped";
    } catch (const std::exception& e) {
        LOG(Error) << "Fatal: " << e.what();
        return 1;
    }
    return 0;
}
