#include "test.h"
#include "app/auth_service.h"
#include "app/errors.h"
#include "infra/repositories.h"
#include "infra/sqlite_db.h"
#include <filesystem>
#include <unistd.h>

using namespace guard;
namespace fs = std::filesystem;

namespace {

// Fresh migrated database in a temp file, removed afterwards.
struct TempDb {
    std::string path;
    std::unique_ptr<Db> db;
    TempDb() {
        path = (fs::temp_directory_path() / ("guard-test-" + std::to_string(::getpid()) + "-" +
                                             std::to_string(counter()++) + ".db")).string();
        db = std::make_unique<Db>(path);
        db->migrate(MIGRATIONS_DIR);
    }
    ~TempDb() {
        db.reset();
        for (const char* suffix : {"", "-wal", "-shm"}) fs::remove(path + suffix);
    }
    static int& counter() { static int c = 0; return c; }
};

} // namespace

TEST(migrations_apply_once) {
    TempDb t;
    CHECK(t.db->schemaVersion() >= 1);
    const int v = t.db->schemaVersion();
    t.db->migrate(MIGRATIONS_DIR);   // idempotent
    CHECK_EQ(t.db->schemaVersion(), v);
}

TEST(fleet_crud_and_assignments) {
    TempDb t;
    FleetRepository repo(*t.db);
    Driver d;
    d.name = "Ali";
    const int64_t did = repo.createDriver(d);
    Vehicle v;
    v.plateNumber = "ABC 123";
    v.plateNormalized = "ABC123";
    const int64_t vid = repo.createVehicle(v);

    repo.assign(did, vid);
    repo.assign(did, vid);   // idempotent
    CHECK_EQ(repo.assignments().size(), std::size_t(1));
    CHECK_EQ(repo.getDriver(did)->vehicles.size(), std::size_t(1));
    CHECK_EQ(repo.getVehicle(vid)->drivers[0].name, std::string("Ali"));
    CHECK(repo.vehicleByPlate("ABC123").has_value());

    CHECK_THROWS(repo.createVehicle(v), DbConstraintError);   // duplicate plate

    CHECK(repo.deleteVehicle(vid));
    CHECK(repo.assignments().empty());                        // cascaded
}

TEST(face_templates_roundtrip_and_cascade) {
    TempDb t;
    FleetRepository repo(*t.db);
    Driver d;
    d.name = "Sara";
    const int64_t did = repo.createDriver(d);
    repo.addTemplates(did, {{0.5f, -1.25f, 3.0f}, {1, 2, 3}}, "test");

    auto ids = repo.identities("test");
    CHECK_EQ(ids.size(), std::size_t(1));
    CHECK_EQ(ids[0].templates.size(), std::size_t(2));
    CHECK_EQ(ids[0].templates[0][1], -1.25f);
    CHECK_EQ(repo.getDriver(did)->templateCount, 2);

    repo.deleteDriver(did);
    CHECK(repo.identities("test").empty());
}

TEST(job_lifecycle) {
    TempDb t;
    JobRepository jobs(*t.db);
    Job a, b;
    a.id = "aaaa"; a.source = "/tmp/a.mp4";
    b.id = "bbbb"; b.source = "/tmp/b.mp4";
    jobs.create(a);
    jobs.create(b);
    CHECK_EQ(jobs.nextQueued()->id, std::string("aaaa"));
    CHECK_EQ(jobs.queuedAhead("bbbb"), 1);

    jobs.markRunning("aaaa");
    CHECK_EQ(jobs.get("aaaa")->attempts, 1);
    CHECK_EQ(jobs.nextQueued()->id, std::string("bbbb"));
    CHECK_EQ(jobs.requeueInterrupted(), 1);
    CHECK_EQ(jobs.get("aaaa")->status, std::string("queued"));

    jobs.complete("aaaa", "authorized", "{}");
    CHECK_EQ(jobs.get("aaaa")->verdict, std::string("authorized"));
    CHECK_EQ(jobs.list(10, 0).size(), std::size_t(2));
}

TEST(sessions_expire_and_users_unique) {
    TempDb t;
    UserRepository users(*t.db);
    User admin;
    admin.username = "admin";
    admin.passwordHash = "hash";
    admin.role = "admin";
    const int64_t uid = users.create(admin);
    CHECK_THROWS(users.create(admin), DbConstraintError);
    users.createSession("tok", uid, 1);
    CHECK(users.sessionUser("tok").has_value());
    users.createSession("old", uid, -1);   // already expired
    CHECK(!users.sessionUser("old").has_value());
    users.deleteSession("tok");
    CHECK(!users.sessionUser("tok").has_value());
}

TEST(alerts_and_access_events) {
    TempDb t;
    EventRepository ev(*t.db);
    AccessEvent e;
    e.verdict = "unauthorized";
    e.plateText = "ABC123";
    const int64_t id = ev.insertAccessEvent(e);
    Alert a = ev.insertAlert("warning", "unauthorized", "test", id);
    CHECK_EQ(ev.listAlerts(true, 10).size(), std::size_t(1));
    CHECK(ev.acknowledge(a.id, "guard1"));
    CHECK(ev.resolve(a.id, "admin"));
    CHECK(ev.listAlerts(true, 10).empty());
    Alert done = *ev.getAlert(a.id);
    CHECK_EQ(done.acknowledgedBy, std::string("guard1"));
    CHECK_EQ(done.resolvedBy, std::string("admin"));
    CHECK_EQ(ev.listAccessEvents(10, 0)[0].plateText, std::string("ABC123"));
}

TEST(cameras_crud_and_unique_names) {
    TempDb t;
    CameraRepository cams(*t.db);
    Camera c;
    c.name = "Gate 1";
    c.source = "csi://0";
    const int64_t id = cams.create(c);
    CHECK_EQ(cams.get(id)->source, std::string("csi://0"));
    CHECK(cams.get(id)->enabled);
    bool threw = false;
    try { cams.create(c); } catch (const DbConstraintError&) { threw = true; }
    CHECK(threw);
    Camera u = *cams.get(id);
    u.enabled = false;
    u.sampleFps = 2.5;
    CHECK(cams.update(u));
    CHECK(!cams.get(id)->enabled);
    CHECK_EQ(cams.get(id)->sampleFps, 2.5);
    CHECK(cams.remove(id));
    CHECK(!cams.get(id));
}

TEST(camera_access_events_filter_and_snapshots) {
    TempDb t;
    CameraRepository cams(*t.db);
    EventRepository ev(*t.db);
    Camera c;
    c.name = "Gate 1";
    c.source = "usb://0";
    const int64_t camId = cams.create(c);

    AccessEvent live;
    live.cameraId = camId;
    live.verdict = "authorized";
    live.authorized = true;
    live.eventTime = "2026-01-02 03:04:05";
    live.endedAt = "2026-01-02 03:04:09";
    live.snapshot = "snapshots/2026-01-02/cam1-1.jpg";
    const int64_t liveId = ev.insertAccessEvent(live);
    AccessEvent upload;
    upload.verdict = "unknown_driver";
    ev.insertAccessEvent(upload);

    auto got = *ev.getAccessEvent(liveId);
    CHECK_EQ(got.cameraName, std::string("Gate 1"));
    CHECK_EQ(got.eventTime, std::string("2026-01-02 03:04:05"));
    CHECK_EQ(ev.listAccessEvents(10, 0).size(), std::size_t(2));
    AccessEventFilter byCam;
    byCam.cameraId = camId;
    CHECK_EQ(ev.listAccessEvents(10, 0, byCam).size(), std::size_t(1));
    AccessEventFilter videos;
    videos.source = "video";
    CHECK_EQ(ev.listAccessEvents(10, 0, videos).size(), std::size_t(1));   // the upload (no end time)
    AccessEventFilter verdict;
    verdict.verdict = "unknown_driver";
    CHECK_EQ(ev.listAccessEvents(10, 0, verdict).size(), std::size_t(1));

    ev.extendAccessEvent(liveId, "2026-01-02 03:05:00");
    CHECK_EQ(ev.getAccessEvent(liveId)->endedAt, std::string("2026-01-02 03:05:00"));
    CHECK_EQ(ev.clearSnapshots("snapshots/2026-01-02/"), 1);
    CHECK(ev.getAccessEvent(liveId)->snapshot.empty());

    cams.remove(camId);                                          // events stay, camera link cleared
    CHECK(!ev.getAccessEvent(liveId)->cameraId.has_value());
    CHECK(ev.getAccessEvent(liveId)->fromCamera());               // ...but it is still a camera visit
    AccessEventFilter cameraOnly;
    cameraOnly.source = "camera";
    CHECK_EQ(ev.listAccessEvents(10, 0, cameraOnly).size(), std::size_t(1));
}

TEST(roles_are_admin_or_guard) {
    TempDb t;
    UserRepository users(*t.db);
    User u;
    u.username = "old-operator";
    u.passwordHash = "x";
    u.role = "operator";
    CHECK_THROWS(users.create(u), DbConstraintError);
    u.role = "guard";
    CHECK(users.create(u) > 0);
}

TEST(user_management_keeps_an_admin_and_revokes_access) {
    TempDb t;
    UserRepository users(*t.db);
    EventRepository events(*t.db);
    AuthService auth(users, events, 12);
    LoginResult first = auth.setup("chief", "chief-pass-1");
    const User admin = first.user;
    CHECK(admin.isAdmin());

    UserInput g;
    g.username = "gate.guard";
    g.fullName = "Night shift";
    g.password = "guard-pass-1";
    User guard = auth.createUser(g, admin);
    CHECK_EQ(guard.role, std::string("guard"));             // default role
    CHECK_THROWS(auth.createUser(g, admin), ConflictError);  // username taken

    // The only admin cannot be demoted, disabled or deleted, and not by themselves.
    UserInput demote;
    demote.role = "guard";
    CHECK_THROWS(auth.updateUser(admin.id, demote, admin), ConflictError);
    CHECK_THROWS(auth.deleteUser(admin.id, admin), ConflictError);

    // A guard's session works until the account is disabled.
    LoginResult gs = auth.login("gate.guard", "guard-pass-1");
    CHECK(auth.authenticate(gs.token).has_value());
    UserInput disable;
    disable.active = false;
    auth.updateUser(guard.id, disable, admin);
    CHECK(!auth.authenticate(gs.token).has_value());
    CHECK_THROWS(auth.login("gate.guard", "guard-pass-1"), UnauthorizedError);

    // Re-enabled with a reset password: old password no longer works.
    UserInput reset;
    reset.active = true;
    reset.password = "new-guard-pass";
    auth.updateUser(guard.id, reset, admin);
    CHECK_THROWS(auth.login("gate.guard", "guard-pass-1"), UnauthorizedError);
    LoginResult again = auth.login("gate.guard", "new-guard-pass");
    CHECK(!again.user.lastLoginAt.empty());

    // Promote the guard; now the first admin may step down.
    UserInput promote;
    promote.role = "admin";
    User second = auth.updateUser(guard.id, promote, admin);
    CHECK(second.isAdmin());
    CHECK_THROWS(auth.updateUser(admin.id, demote, admin), ConflictError);   // not their own role
    auth.updateUser(admin.id, demote, second);
    CHECK_EQ(auth.getUser(admin.id).role, std::string("guard"));

    // Own password change needs the current password.
    CHECK_THROWS(auth.changeOwnPassword(second, "wrong-password", "another-pass"), ValidationError);
    auth.changeOwnPassword(second, "new-guard-pass", "another-pass");
    CHECK(auth.login("gate.guard", "another-pass").user.isAdmin());
}

TEST(community_profile_roundtrip) {
    TempDb t;
    CommunityRepository repo(*t.db);
    CHECK(!repo.get().configured());          // created empty by the migration
    Community c;
    c.name = "Pak-Austria Fachhochschule";
    c.helpline = "1122";
    c.logoFile = "logo.png";
    repo.save(c);
    const Community back = repo.get();
    CHECK(back.configured());
    CHECK_EQ(back.name, std::string("Pak-Austria Fachhochschule"));
    CHECK_EQ(back.helpline, std::string("1122"));
    CHECK_EQ(back.logoFile, std::string("logo.png"));
    c.logoFile.clear();
    repo.save(c);
    CHECK(repo.get().logoFile.empty());
}
