#pragma once
#include "domain/model.h"
#include <optional>
#include <vector>

namespace guard {

struct FaceMatch {
    int64_t driverId = 0;
    std::string name;
    std::string status;
    float similarity = 0.f;
};

// ---------------------------------------------------------------------------
// FaceMatcher: cosine similarity of a query embedding against every enrolled
// template; a driver's score is its best template. Immutable after
// construction, so one instance can be shared across threads.
// ---------------------------------------------------------------------------
class FaceMatcher {
public:
    FaceMatcher(std::vector<Identity> identities, float threshold);

    // Best driver with similarity >= threshold, if any.
    std::optional<FaceMatch> match(const std::vector<float>& embedding) const;

    std::size_t size() const { return identities_.size(); }
    float threshold() const { return threshold_; }

private:
    std::vector<Identity> identities_;   // templates stored L2-normalized
    float threshold_;
};

// Scales v to unit length in place (no-op for a zero vector).
void l2Normalize(std::vector<float>& v);

} // namespace guard
