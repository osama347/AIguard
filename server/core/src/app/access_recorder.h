#pragma once
#include "domain/access_policy.h"
#include "domain/model.h"
#include "infra/repositories.h"
#include <optional>

namespace guard {

struct RecordedAccess {
    AccessEvent event;             // as stored (with joined names)
    std::optional<Alert> alert;    // raised for this event, if any
};

// Stores the outcome of one decision (from a test video or a camera visit) in
// the access log. Camera visits also raise the alert they call for:
// blacklisted -> critical; unauthorized, unknown driver -> warning. Test videos
// never alert (they must not alarm the guards on duty). `base` carries the
// source fields (job_id or camera_id, times, snapshot, details).
RecordedAccess recordAccess(EventRepository& events, AccessEvent base, const Decision& decision,
                            const Evidence& evidence);

} // namespace guard
