#include "app/access_recorder.h"

namespace guard {

RecordedAccess recordAccess(EventRepository& events, AccessEvent ae, const Decision& decision,
                            const Evidence& evidence) {
    ae.verdict = decision.verdict;
    ae.authorized = decision.authorized;
    if (decision.driver) ae.driverId = decision.driver->driverId;
    if (decision.vehicle) {
        ae.vehicleId = decision.vehicle->vehicleId;
        ae.plateText = decision.vehicle->text;
    } else if (!evidence.plates().empty()) {
        // Record the most-read unregistered plate for the log.
        int best = 0;
        for (const auto& [text, t] : evidence.plates())
            if (t.reads > best) { best = t.reads; ae.plateText = text; }
    }
    const int64_t id = events.insertAccessEvent(ae);

    RecordedAccess out;
    out.event = events.getAccessEvent(id).value_or(ae);
    if (!ae.fromCamera()) return out;   // test video: logged, no alert
    std::string where = ae.cameraId && !out.event.cameraName.empty() ? " at " + out.event.cameraName : "";
    const std::string message = decision.reason + where;
    if (decision.blacklisted) out.alert = events.insertAlert("critical", "blacklisted", message, id);
    else if (decision.verdict == "unauthorized") out.alert = events.insertAlert("warning", "unauthorized", message, id);
    else if (decision.verdict == "unknown_driver") out.alert = events.insertAlert("warning", "unknown_driver", message, id);
    return out;
}

} // namespace guard
