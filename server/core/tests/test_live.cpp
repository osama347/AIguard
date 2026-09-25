#include "test.h"
#include "app/event_bus.h"
#include "domain/visit_tracker.h"

using namespace guard;

namespace {

FrameObservation plateFrame(int64_t ts, const std::string& text, uint64_t frame = 0, bool image = true) {
    FrameObservation o;
    o.tsMs = ts;
    o.frame = frame;
    o.hasImage = image;
    PlateObservation p;
    p.text = text;
    p.confidence = 0.9f;
    o.plates.push_back(p);
    return o;
}

FrameObservation emptyFrame(int64_t ts) {
    FrameObservation o;
    o.tsMs = ts;
    return o;
}

VisitConfig cfg() { return VisitConfig{4000, 60000, 2}; }

} // namespace

TEST(visit_opens_on_detection_and_closes_after_gap) {
    VisitTracker t(cfg());
    CHECK(!t.observe(emptyFrame(0)));
    CHECK(!t.active());
    for (int i = 0; i < 5; ++i) CHECK(!t.observe(plateFrame(1000 + i * 200, "ABC123", i + 1)));
    CHECK(t.active());
    CHECK(!t.observe(emptyFrame(4000)));          // within the gap: still the same visit
    auto v = t.observe(emptyFrame(6000));          // 5.2 s after the last detection
    CHECK(v.has_value());
    CHECK(!t.active());
    CHECK_EQ(v->frames, 5);
    CHECK_EQ(v->startMs, int64_t(1000));
    CHECK_EQ(v->endMs, int64_t(1800));
    CHECK_EQ(v->evidence.plates().at("ABC123").reads, 5);
}

TEST(visit_with_too_few_frames_is_noise) {
    VisitTracker t(cfg());
    t.observe(plateFrame(0, "ABC123"));
    CHECK(!t.observe(emptyFrame(10000)).has_value());   // one frame < min_frames 2
    CHECK(!t.active());
}

TEST(long_visit_is_split) {
    VisitTracker t(VisitConfig{4000, 10000, 1});
    std::optional<Visit> split;
    for (int64_t ts = 0; ts <= 12000; ts += 500)
        if (auto v = t.observe(plateFrame(ts, "ABC123"))) split = v;
    CHECK(split.has_value());
    CHECK(split->endMs - split->startMs < 10000);
    CHECK(t.active());                               // the next visit continues
}

TEST(visit_keeps_best_snapshot_frame) {
    VisitTracker t(cfg());
    FrameObservation unreadable = plateFrame(0, "", 1);
    t.observe(unreadable);
    CHECK(t.bestChanged());
    t.observe(plateFrame(200, "ABC123", 2));
    CHECK(t.bestChanged());                          // a readable plate beats an unreadable one
    t.observe(plateFrame(400, "ABC123", 3, false));  // no image kept: cannot be the snapshot
    CHECK(!t.bestChanged());
    auto v = t.flush();
    CHECK(v.has_value());
    CHECK_EQ(v->best->frame, uint64_t(2));
    CHECK(v->meaningful());
}

TEST(unreadable_plates_only_is_not_meaningful) {
    VisitTracker t(cfg());
    t.observe(plateFrame(0, "", 1));
    t.observe(plateFrame(200, "", 2));
    auto v = t.flush();
    CHECK(v.has_value());
    CHECK(!v->meaningful());
}

TEST(event_bus_topics_and_transient_buffer) {
    EventBus bus(10);
    const uint64_t start = bus.lastId();
    bus.publish("job.completed", {{"n", 1}}, "job1");
    for (int i = 0; i < 400; ++i) bus.publishTransient("camera.frame", {{"i", i}}, "camera:1");
    bus.publish("alert.created", {{"n", 2}});

    auto all = bus.waitAfter(start, std::chrono::milliseconds(0), "");
    CHECK_EQ(all.front().type, std::string("job.completed"));   // not pushed out by 400 frames
    CHECK_EQ(all.back().type, std::string("alert.created"));
    for (std::size_t i = 1; i < all.size(); ++i) CHECK(all[i - 1].id < all[i].id);

    auto cam = bus.waitAfter(start, std::chrono::milliseconds(0), "camera:1");
    CHECK(!cam.empty());
    for (const auto& e : cam) CHECK_EQ(e.type, std::string("camera.frame"));
    CHECK_EQ(bus.waitAfter(start, std::chrono::milliseconds(0), "job1").size(), std::size_t(1));
}
