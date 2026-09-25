#pragma once
#include "app/auth_service.h"
#include "app/camera_service.h"
#include "app/event_bus.h"
#include "app/fleet_service.h"
#include "app/job_service.h"
#include "common/config.h"
#include <chrono>

namespace httplib { class Server; }

namespace guard {

struct ApiDeps {
    const CoreConfig& cfg;
    AuthService& auth;
    FleetService& fleet;
    JobService& jobs;
    CameraService& cameras;
    CommunityRepository& community;
    EventRepository& events;
    StatsRepository& stats;
    InferenceClient& inference;
    EventBus& bus;
};

// Registers the public REST + SSE API (/api/v1, see api/openapi.yaml) and the
// static frontend on `svr`.
void registerApi(httplib::Server& svr, ApiDeps deps);

} // namespace guard
