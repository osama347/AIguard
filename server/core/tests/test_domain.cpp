#include "test.h"
#include "domain/access_policy.h"
#include "domain/face_matcher.h"
#include "domain/plate.h"
#include "infra/crypto.h"

using namespace guard;

// ---------------------------------------------------------------- plates

TEST(plate_normalization_strips_and_uppercases) {
    CHECK_EQ(normalizePlate("abc-12 3"), std::string("ABC123"));
    CHECK_EQ(normalizePlate("  lez 1234 "), std::string("LEZ1234"));
    CHECK_EQ(normalizePlate("--"), std::string(""));
}

TEST(plate_shape_is_letters_then_digits) {
    CHECK(isPlausiblePlate("FB393"));
    CHECK(isPlausiblePlate("LEE9876"));
    CHECK(isPlausiblePlate("ABC1"));
    CHECK(!isPlausiblePlate("A123"));
    CHECK(!isPlausiblePlate("ABCD123"));
    CHECK(!isPlausiblePlate("LEE12345"));
    CHECK(!isPlausiblePlate("LEE"));
    CHECK(!isPlausiblePlate("L1E123"));
}

// ---------------------------------------------------------------- matcher

namespace {
Identity identity(int64_t id, const std::string& name, std::vector<std::vector<float>> t,
                  const std::string& status = "active") {
    return Identity{id, name, status, std::move(t)};
}
}

TEST(matcher_picks_best_driver_above_threshold) {
    FaceMatcher m({identity(1, "ali", {{1, 0, 0}}), identity(2, "sara", {{0, 1, 0}, {0.7f, 0.7f, 0}})}, 0.8f);
    auto r = m.match({0.1f, 1.0f, 0});
    CHECK(r.has_value());
    CHECK_EQ(r->driverId, int64_t(2));
    CHECK(r->similarity > 0.99f);
}

TEST(matcher_rejects_below_threshold_and_wrong_dimension) {
    FaceMatcher m({identity(1, "ali", {{1, 0, 0}})}, 0.8f);
    CHECK(!m.match({0, 1, 0}).has_value());          // orthogonal
    CHECK(!m.match({1, 0, 0, 0}).has_value());       // other model's dimension
}

TEST(matcher_is_scale_invariant) {
    FaceMatcher m({identity(1, "ali", {{2, 0, 0}})}, 0.9f);
    auto r = m.match({50, 1, 0});
    CHECK(r.has_value());
    CHECK_EQ(r->name, std::string("ali"));
}

// ---------------------------------------------------------------- policy

namespace {
const PolicyConfig kPolicy{1, 1};

FaceMatch face(int64_t id, const std::string& name, float sim, const std::string& status = "active") {
    return FaceMatch{id, name, status, sim};
}
}

TEST(policy_authorized_when_assigned) {
    Evidence e;
    e.addFace(face(1, "ali", 0.8f));
    e.addPlate("ABC123", 10, "active");
    auto d = decideAccess(e, {{1, 10}}, kPolicy);
    CHECK_EQ(d.verdict, std::string("authorized"));
    CHECK(d.authorized);
}

TEST(policy_unauthorized_when_not_assigned) {
    Evidence e;
    e.addFace(face(1, "ali", 0.8f));
    e.addPlate("ABC123", 10, "active");
    auto d = decideAccess(e, {{2, 10}}, kPolicy);
    CHECK_EQ(d.verdict, std::string("unauthorized"));
    CHECK(!d.blacklisted);
}

TEST(policy_blacklisted_driver_always_unauthorized) {
    Evidence e;
    e.addFace(face(1, "ali", 0.8f, "blacklisted"));
    auto d = decideAccess(e, {}, kPolicy);
    CHECK_EQ(d.verdict, std::string("unauthorized"));
    CHECK(d.blacklisted);
}

TEST(policy_blacklisted_vehicle_even_with_assignment) {
    Evidence e;
    e.addFace(face(1, "ali", 0.8f));
    e.addPlate("ABC123", 10, "blacklisted");
    auto d = decideAccess(e, {{1, 10}}, kPolicy);
    CHECK_EQ(d.verdict, std::string("unauthorized"));
    CHECK(d.blacklisted);
}

TEST(policy_inactive_driver_unauthorized) {
    Evidence e;
    e.addFace(face(1, "ali", 0.8f, "inactive"));
    e.addPlate("ABC123", 10, "active");
    CHECK_EQ(decideAccess(e, {{1, 10}}, kPolicy).verdict, std::string("unauthorized"));
}

TEST(policy_unknown_variants) {
    Evidence onlyPlate;
    onlyPlate.addFace(std::nullopt);
    onlyPlate.addPlate("ABC123", 10, "active");
    CHECK_EQ(decideAccess(onlyPlate, {}, kPolicy).verdict, std::string("unknown_driver"));

    Evidence onlyFace;
    onlyFace.addFace(face(1, "ali", 0.9f));
    onlyFace.addPlate("ZZZ999", std::nullopt, "");
    CHECK_EQ(decideAccess(onlyFace, {}, kPolicy).verdict, std::string("unknown_vehicle"));

    Evidence nothing;
    CHECK_EQ(decideAccess(nothing, {}, kPolicy).verdict, std::string("unknown_both"));
}

TEST(policy_majority_across_frames_wins) {
    Evidence e;
    e.addFace(face(1, "ali", 0.95f));     // one strong frame
    e.addFace(face(2, "sara", 0.70f));    // three weaker frames
    e.addFace(face(2, "sara", 0.72f));
    e.addFace(face(2, "sara", 0.71f));
    e.addPlate("ABC123", 10, "active");
    e.addPlate("ABC128", 11, "active");   // one misread of another registered plate
    e.addPlate("ABC123", 10, "active");
    auto d = decideAccess(e, {{2, 10}}, kPolicy);
    CHECK_EQ(d.driver->driverId, int64_t(2));
    CHECK_EQ(*d.vehicle->vehicleId, int64_t(10));
    CHECK_EQ(d.verdict, std::string("authorized"));
}

TEST(policy_min_frames_threshold) {
    Evidence e;
    e.addFace(face(1, "ali", 0.9f));
    e.addPlate("ABC123", 10, "active");
    auto d = decideAccess(e, {{1, 10}}, PolicyConfig{2, 1});
    CHECK_EQ(d.verdict, std::string("unknown_driver"));
}

// ---------------------------------------------------------------- crypto

TEST(sha256_known_vectors) {
    CHECK_EQ(crypto::sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(crypto::sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(crypto::sha256Hex(std::string(1000, 'a')),
             std::string("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"));
}

TEST(pbkdf2_rfc_vector) {
    // RFC 7914 section 11 PBKDF2-HMAC-SHA256 test vector.
    auto out = crypto::pbkdf2Sha256("passwd", {'s', 'a', 'l', 't'}, 1, 64);
    CHECK_EQ(crypto::toHex(out).substr(0, 32), std::string("55ac046e56e3089fec1691c22544b605"));
}

TEST(password_hash_roundtrip) {
    const std::string h = crypto::hashPassword("correct horse");
    CHECK(crypto::verifyPassword("correct horse", h));
    CHECK(!crypto::verifyPassword("wrong horse", h));
    CHECK(!crypto::verifyPassword("correct horse", "garbage"));
    CHECK(h != crypto::hashPassword("correct horse"));   // salted
}
