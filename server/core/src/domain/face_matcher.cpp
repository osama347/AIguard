#include "domain/face_matcher.h"
#include <cmath>

namespace guard {

void l2Normalize(std::vector<float>& v) {
    double sum = 0;
    for (float x : v) sum += static_cast<double>(x) * x;
    if (sum <= 1e-18) return;
    const float inv = static_cast<float>(1.0 / std::sqrt(sum));
    for (float& x : v) x *= inv;
}

FaceMatcher::FaceMatcher(std::vector<Identity> identities, float threshold)
    : identities_(std::move(identities)), threshold_(threshold) {
    for (auto& id : identities_)
        for (auto& t : id.templates) l2Normalize(t);
}

std::optional<FaceMatch> FaceMatcher::match(const std::vector<float>& embedding) const {
    std::vector<float> q = embedding;
    l2Normalize(q);

    std::optional<FaceMatch> best;
    for (const auto& id : identities_) {
        for (const auto& t : id.templates) {
            if (t.size() != q.size()) continue;   // template from another model
            float dot = 0.f;
            for (std::size_t i = 0; i < q.size(); ++i) dot += q[i] * t[i];
            if (dot >= threshold_ && (!best || dot > best->similarity))
                best = FaceMatch{id.driverId, id.name, id.status, dot};
        }
    }
    return best;
}

} // namespace guard
