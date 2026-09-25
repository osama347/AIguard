#pragma once
#include "app/event_bus.h"
#include "app/fleet_service.h"
#include "common/config.h"
#include "infra/inference_client.h"
#include "infra/repositories.h"
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace guard {

// ---------------------------------------------------------------------------
// CameraService: live cameras, analysed around the clock.
//
// Per enabled camera, a supervisor thread keeps a stream registered in
// guard-inference (re-registering after either service restarts), consumes
// its frames, matches faces/plates, splits the flow into visits
// (VisitTracker), decides each visit and records it in the access log with a
// snapshot. A monitor thread raises/resolves "camera offline" alerts and
// enforces snapshot retention.
// ---------------------------------------------------------------------------
class CameraService {
public:
    CameraService(CameraRepository& cameras, FleetService& fleet, EventRepository& events,
                  InferenceClient& inference, EventBus& bus, const CoreConfig& cfg);
    ~CameraService();

    void start();
    void stop();

    std::vector<Camera> list() { return cameras_.list(); }
    Camera get(int64_t id);
    Camera create(Camera c, const std::string& actor);
    Camera update(Camera c, const std::string& actor);
    void remove(int64_t id, const std::string& actor);

    // {state, message, analysis_fps, width, height, last_frame_at, visit_active}
    nlohmann::json status(int64_t id);
    nlohmann::json probe(const std::string& source);
    // Newest preview JPEG newer than `after`; nullopt if none within ~3 s.
    std::optional<std::pair<uint64_t, std::string>> preview(int64_t id, uint64_t after);
    std::string snapshotFile(const AccessEvent& e) const;   // absolute path, "" if none

    static std::string streamId(int64_t cameraId) { return "cam-" + std::to_string(cameraId); }

    class Supervisor;

private:
    void startSupervisor(const Camera& c);
    void stopSupervisor(int64_t id);
    void monitorLoop();
    void enforceRetention();
    void validate(Camera& c);

    CameraRepository& cameras_;
    FleetService& fleet_;
    EventRepository& events_;
    InferenceClient& inference_;
    EventBus& bus_;
    const CoreConfig& cfg_;

    std::mutex mtx_;
    std::map<int64_t, std::shared_ptr<Supervisor>> supervisors_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::thread monitor_;
};

} // namespace guard
