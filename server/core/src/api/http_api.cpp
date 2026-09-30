#include "api/http_api.h"
#include "app/errors.h"
#include "app/serialization.h"
#include "common/log.h"
#include "infra/sqlite_db.h"
#include <httplib.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <map>
#include <ctime>

namespace fs = std::filesystem;
using json = nlohmann::json;

#ifndef GUARD_VERSION
#define GUARD_VERSION "dev"
#endif

namespace guard {

namespace {

using Req = httplib::Request;
using Res = httplib::Response;

void sendJson(Res& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

void sendError(Res& res, int status, const std::string& code, const std::string& message) {
    sendJson(res, {{"error", code}, {"message", message}}, status);
}

// Runs a handler, mapping application errors to HTTP responses.
template <typename Fn> void guarded(Res& res, Fn&& fn) {
    try {
        fn();
    } catch (const ValidationError& e) { sendError(res, 400, "validation_error", e.what());
    } catch (const UnauthorizedError& e) { sendError(res, 401, "unauthorized", e.what());
    } catch (const ForbiddenError& e) { sendError(res, 403, "forbidden", e.what());
    } catch (const NotFoundError& e) { sendError(res, 404, "not_found", e.what());
    } catch (const ConflictError& e) { sendError(res, 409, "conflict", e.what());
    } catch (const UnavailableError& e) { sendError(res, 503, "unavailable", e.what());
    } catch (const json::exception& e) { sendError(res, 400, "bad_request", std::string("invalid JSON: ") + e.what());
    } catch (const DbConstraintError& e) { sendError(res, 409, "conflict", e.what());
    } catch (const std::exception& e) {
        LOG(Error) << "Unhandled error: " << e.what();
        sendError(res, 500, "internal_error", "internal server error");
    }
}

json parseBody(const Req& req) {
    if (req.body.empty()) return json::object();
    json j = json::parse(req.body);
    if (!j.is_object()) throw ValidationError("request body must be a JSON object");
    return j;
}

int64_t idParam(const Req& req, int index = 1) {
    try { return std::stoll(req.matches[index].str()); }
    catch (...) { throw ValidationError("invalid id"); }
}

int intParam(const Req& req, const char* name, int def, int max) {
    if (!req.has_param(name)) return def;
    try { return std::max(0, std::min(max, std::stoi(req.get_param_value(name)))); }
    catch (...) { throw ValidationError(std::string("invalid ") + name); }
}

std::string bearerToken(const Req& req) {
    const std::string h = req.get_header_value("Authorization");
    if (h.rfind("Bearer ", 0) == 0) return h.substr(7);
    // EventSource and <video src> cannot set headers.
    if (req.has_param("access_token")) return req.get_param_value("access_token");
    return "";
}

bool isLoopback(const Req& req) {
    return req.remote_addr == "127.0.0.1" || req.remote_addr == "::1" || req.remote_addr == "::ffff:127.0.0.1";
}

std::string contentTypeFor(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".mp4" || ext == ".m4v") return "video/mp4";
    if (ext == ".webm") return "video/webm";
    if (ext == ".mov") return "video/quicktime";
    if (ext == ".mkv") return "video/x-matroska";
    if (ext == ".avi") return "video/x-msvideo";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".bmp") return "image/bmp";
    if (ext == ".webp") return "image/webp";
    return "application/octet-stream";
}

Driver driverFromJson(const json& j, Driver d = {}) {
    if (j.contains("name")) d.name = j["name"].get<std::string>();
    if (j.contains("status")) d.status = j["status"].get<std::string>();
    if (j.contains("phone")) d.phone = j["phone"].get<std::string>();
    if (j.contains("notes")) d.notes = j["notes"].get<std::string>();
    return d;
}

Vehicle vehicleFromJson(const json& j, Vehicle v = {}) {
    if (j.contains("plate_number")) v.plateNumber = j["plate_number"].get<std::string>();
    if (j.contains("make")) v.make = j["make"].get<std::string>();
    if (j.contains("model")) v.model = j["model"].get<std::string>();
    if (j.contains("color")) v.color = j["color"].get<std::string>();
    if (j.contains("status")) v.status = j["status"].get<std::string>();
    return v;
}

Camera cameraFromJson(const json& j, Camera c = {}) {
    if (j.contains("name")) c.name = j["name"].get<std::string>();
    if (j.contains("source")) c.source = j["source"].get<std::string>();
    if (j.contains("enabled")) c.enabled = j["enabled"].get<bool>();
    if (j.contains("sample_fps")) c.sampleFps = j["sample_fps"].get<double>();
    return c;
}

json loginJson(const LoginResult& r) {
    return {{"token", r.token}, {"user", toJson(r.user)}, {"expires_in_hours", r.expiresInHours}};
}

// Local-time labels for the last `n` hours ("YYYY-MM-DD HH:00") or days ("YYYY-MM-DD"), oldest first.
std::vector<std::string> timeBuckets(int n, bool hours) {
    std::vector<std::string> out;
    const std::time_t now = std::time(nullptr);
    for (int i = n - 1; i >= 0; --i) {
        std::time_t t = now - static_cast<std::time_t>(i) * (hours ? 3600 : 86400);
        std::tm tm{};
        localtime_r(&t, &tm);
        char buf[20];
        std::strftime(buf, sizeof buf, hours ? "%Y-%m-%d %H:00" : "%Y-%m-%d", &tm);
        out.emplace_back(buf);
    }
    return out;
}

// [{bucket, total, authorized, unauthorized, ...}] with every bucket present.
json bucketSeries(const std::vector<std::string>& labels, const std::vector<VerdictBucket>& rows) {
    static const char* verdicts[] = {"authorized", "unauthorized", "unknown_driver", "unknown_vehicle", "unknown_both"};
    std::map<std::string, json> byLabel;
    for (const auto& l : labels) {
        json b{{"bucket", l}, {"total", 0}};
        for (const char* v : verdicts) b[v] = 0;
        byLabel[l] = b;
    }
    for (const auto& r : rows) {
        auto it = byLabel.find(r.bucket);
        if (it == byLabel.end()) continue;
        it->second[r.verdict] = it->second.value(r.verdict, 0) + r.count;
        it->second["total"] = it->second["total"].get<int>() + r.count;
    }
    json arr = json::array();
    for (const auto& l : labels) arr.push_back(byLabel[l]);
    return arr;
}

uintmax_t directorySize(const std::string& dir) {
    std::error_code ec;
    uintmax_t total = 0;
    if (!fs::is_directory(dir, ec)) return 0;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) total += it->file_size(ec);
    return total;
}

// Compares without leaking where the first difference is.
bool constantTimeEquals(const std::string& a, const std::string& b) {
    unsigned char diff = a.size() == b.size() ? 0 : 1;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

// The installer's one-time code that lets the first admin be created from another machine
// (GUARD_SETUP_CODE in /etc/guard/guard.env). Wrong guesses are throttled: 5 per 5 minutes.
void checkSetupCode(const std::string& given) {
    static std::mutex m;
    static int failures = 0;
    static std::chrono::steady_clock::time_point windowStart;
    std::lock_guard<std::mutex> lk(m);
    const auto now = std::chrono::steady_clock::now();
    if (now - windowStart > std::chrono::minutes(5)) { failures = 0; windowStart = now; }
    if (failures >= 5) throw ForbiddenError("too many wrong setup codes; wait a few minutes and try again");
    const char* code = std::getenv("GUARD_SETUP_CODE");
    if (!code || !*code || !constantTimeEquals(given, code)) {
        ++failures;
        throw ForbiddenError("creating the first account from another machine needs the setup code "
                             "shown when the server was installed (on the Jetson: sudo cat /etc/guard/guard.env)");
    }
}

// Wrong authorization-code guesses are throttled the same way as the setup code
// (5 per 5 minutes): the code is much shorter than a password.
class RateLimiter {
public:
    void guard() {
        std::lock_guard<std::mutex> lk(m_);
        resetIfExpired();
        if (failures_ >= 5) throw ForbiddenError("too many wrong codes; wait a few minutes and try again");
    }
    void fail() {
        std::lock_guard<std::mutex> lk(m_);
        resetIfExpired();
        ++failures_;
    }

private:
    void resetIfExpired() {
        const auto now = std::chrono::steady_clock::now();
        if (now - windowStart_ > std::chrono::minutes(5)) { failures_ = 0; windowStart_ = now; }
    }
    std::mutex m_;
    int failures_ = 0;
    std::chrono::steady_clock::time_point windowStart_;
};

RateLimiter& authCodeLimiter() {
    static RateLimiter r;
    return r;
}

std::string trimmed(std::string v) {
    const auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!v.empty() && ws(v.back())) v.pop_back();
    size_t i = 0;
    while (i < v.size() && ws(v[i])) ++i;
    return v.substr(i);
}

// Applies the fields present in `j`; every text field is trimmed and length-limited.
Community communityFromJson(const json& j, Community c = {}) {
    const auto field = [&](const char* key, std::string& out, size_t max) {
        if (!j.contains(key)) return;
        if (!j[key].is_string()) throw ValidationError(std::string(key) + " must be text");
        out = trimmed(j[key].get<std::string>());
        if (out.size() > max) throw ValidationError(std::string(key) + " is too long");
    };
    field("name", c.name, 120);
    field("address", c.address, 300);
    field("city", c.city, 100);
    field("country", c.country, 100);
    field("helpline", c.helpline, 60);
    field("email", c.email, 120);
    field("website", c.website, 200);
    return c;
}

json communityJson(const Community& c) {
    return {{"configured", c.configured()}, {"name", c.name}, {"address", c.address}, {"city", c.city},
            {"country", c.country}, {"helpline", c.helpline}, {"email", c.email}, {"website", c.website},
            {"logo_url", c.logoFile.empty() ? json(nullptr) : json("/api/v1/community/logo?v=" + c.updatedAt)}};
}

// PNG, JPEG or WebP only (SVG can carry scripts), judged by content, not by file name.
const char* imageExtension(const std::string& b) {
    if (b.size() > 8 && b.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) return ".png";
    if (b.size() > 3 && b.compare(0, 3, "\xff\xd8\xff") == 0) return ".jpg";
    if (b.size() > 12 && b.compare(0, 4, "RIFF") == 0 && b.compare(8, 4, "WEBP") == 0) return ".webp";
    return nullptr;
}

} // namespace

void registerApi(httplib::Server& svr, ApiDeps d) {
    const auto started = std::chrono::steady_clock::now();
    auto deps = std::make_shared<ApiDeps>(d);

    // Authenticated user for this request, or throws UnauthorizedError.
    auto requireUser = [deps](const Req& req) -> User {
        auto u = deps->auth.authenticate(bearerToken(req));
        if (!u) throw UnauthorizedError("login required");
        return *u;
    };
    auto requireAdmin = [requireUser](const Req& req) -> User {
        User u = requireUser(req);
        if (!u.isAdmin()) throw ForbiddenError("this needs an administrator account");
        return u;
    };

    // ------------------------------------------------------------ CORS
    svr.set_post_routing_handler([](const Req&, Res& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type, Last-Event-ID");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Expose-Headers", "Content-Length, Content-Range");
    });
    svr.Options(R"(/api/.*)", [](const Req&, Res& res) { res.status = 204; });

    // ------------------------------------------------------------ system
    svr.Get("/api/v1/health", [deps, started](const Req&, Res& res) {
        guarded(res, [&] {
            auto inf = deps->inference.health();
            std::error_code ec;
            auto space = fs::space(deps->cfg.dataDir(), ec);
            auto up = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started);
            sendJson(res, {{"status", inf.status == "ok" ? "ok" : "degraded"},
                           {"version", GUARD_VERSION},
                           {"uptime_s", up.count()},
                           {"setup_required", deps->auth.setupRequired()},
                           {"inference", {{"status", inf.status}, {"message", inf.message}}},
                           {"disk", {{"free_bytes", ec ? 0 : space.available},
                                     {"total_bytes", ec ? 0 : space.capacity}}}});
        });
    });

    svr.Get("/api/v1/system", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            requireUser(req);
            json models = nullptr;
            try { models = deps->inference.models(); } catch (...) {}
            sendJson(res, {{"version", GUARD_VERSION}, {"models", models}, {"runner", deps->jobs.runnerStatus()},
                           {"policy", {{"match_threshold", deps->cfg.policy.matchThreshold},
                                       {"min_face_frames", deps->cfg.policy.minFaceFrames},
                                       {"min_plate_reads", deps->cfg.policy.minPlateReads},
                                       {"sample_fps", deps->cfg.inference.sampleFps}}},
                           {"live", {{"visit_gap_s", deps->cfg.live.visitGapS},
                                     {"max_visit_s", deps->cfg.live.maxVisitS},
                                     {"repeat_suppress_s", deps->cfg.live.repeatSuppressS},
                                     {"snapshot_retention_days", deps->cfg.live.snapshotRetentionDays},
                                     {"cameras", deps->cameras.list().size()}}}});
        });
    });

    // ------------------------------------------------------------ first-run setup
    svr.Post("/api/v1/setup", [deps](const Req& req, Res& res) {
        guarded(res, [&] {
            json b = parseBody(req);
            if (!isLoopback(req)) checkSetupCode(b.value("setup_code", ""));
            // Validate the community details first so a typo cannot leave a half-finished setup.
            std::optional<Community> community;
            if (b.contains("community") && b["community"].is_object())
                community = communityFromJson(b["community"], deps->community.get());
            auto result = deps->auth.setup(b.value("username", ""), b.value("password", ""));
            if (community) deps->community.save(*community);
            sendJson(res, loginJson(result), 201);
        });
    });

    // ------------------------------------------------------------ community profile
    // Public read: the login screen shows the community's name and logo before anyone signs in.
    svr.Get("/api/v1/community", [deps](const Req&, Res& res) {
        guarded(res, [&] { sendJson(res, communityJson(deps->community.get())); });
    });

    svr.Put("/api/v1/community", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            Community c = communityFromJson(parseBody(req), deps->community.get());
            if (c.name.empty()) throw ValidationError("the community needs a name");
            deps->community.save(c);
            deps->events.audit(u.username, "update", "community", "1");
            sendJson(res, communityJson(deps->community.get()));
        });
    });

    svr.Get("/api/v1/community/logo", [deps](const Req&, Res& res) {
        guarded(res, [&] {
            const Community c = deps->community.get();
            const std::string path = deps->cfg.brandingDir() + "/" + c.logoFile;
            if (c.logoFile.empty() || !fs::exists(path)) throw NotFoundError("no logo");
            const std::string ext = fs::path(c.logoFile).extension().string();
            res.set_header("Cache-Control", "public, max-age=86400");
            res.set_header("X-Content-Type-Options", "nosniff");
            res.set_file_content(path, ext == ".png" ? "image/png" : ext == ".webp" ? "image/webp" : "image/jpeg");
        });
    });

    svr.Put("/api/v1/community/logo", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            if (!req.is_multipart_form_data() || !req.form.has_file("logo"))
                throw ValidationError("expected multipart/form-data with a 'logo' file");
            const auto file = req.form.get_file("logo");
            if (file.content.size() > (2u << 20)) throw ValidationError("the logo must be smaller than 2 MB");
            const char* ext = imageExtension(file.content);
            if (!ext) throw ValidationError("the logo must be a PNG, JPEG or WebP image");
            std::error_code ec;
            fs::create_directories(deps->cfg.brandingDir(), ec);
            Community c = deps->community.get();
            if (!c.logoFile.empty()) fs::remove(deps->cfg.brandingDir() + "/" + c.logoFile, ec);
            c.logoFile = std::string("logo") + ext;
            std::ofstream out(deps->cfg.brandingDir() + "/" + c.logoFile, std::ios::binary | std::ios::trunc);
            out.write(file.content.data(), static_cast<std::streamsize>(file.content.size()));
            if (!out) throw UnavailableError("could not store the logo");
            out.close();
            deps->community.save(c);
            deps->events.audit(u.username, "update", "community_logo", "1");
            sendJson(res, communityJson(deps->community.get()));
        });
    });

    svr.Delete("/api/v1/community/logo", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            Community c = deps->community.get();
            std::error_code ec;
            if (!c.logoFile.empty()) fs::remove(deps->cfg.brandingDir() + "/" + c.logoFile, ec);
            c.logoFile.clear();
            deps->community.save(c);
            deps->events.audit(u.username, "delete", "community_logo", "1");
            sendJson(res, communityJson(c));
        });
    });

    // ------------------------------------------------------------ auth
    svr.Post("/api/v1/auth/login", [deps](const Req& req, Res& res) {
        guarded(res, [&] {
            json b = parseBody(req);
            sendJson(res, loginJson(deps->auth.login(b.value("username", ""), b.value("password", ""))));
        });
    });

    svr.Post("/api/v1/auth/logout", [deps](const Req& req, Res& res) {
        guarded(res, [&] {
            deps->auth.logout(bearerToken(req));
            res.status = 204;
        });
    });

    svr.Get("/api/v1/auth/me", [requireUser](const Req& req, Res& res) {
        guarded(res, [&] { sendJson(res, toJson(requireUser(req))); });
    });

    svr.Post("/api/v1/auth/password", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireUser(req);
            json b = parseBody(req);
            deps->auth.changeOwnPassword(u, b.value("current_password", ""), b.value("new_password", ""));
            res.status = 204;
        });
    });

    // ------------------------------------------------------------ users (admin)
    auto userInput = [](const json& b) {
        UserInput in;
        if (b.contains("username")) in.username = b["username"].get<std::string>();
        if (b.contains("full_name")) in.fullName = b["full_name"].get<std::string>();
        if (b.contains("password") && !b["password"].is_null()) in.password = b["password"].get<std::string>();
        if (b.contains("role")) in.role = b["role"].get<std::string>();
        if (b.contains("active")) in.active = b["active"].get<bool>();
        return in;
    };

    svr.Get("/api/v1/users", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            requireAdmin(req);
            json arr = json::array();
            for (const auto& u : deps->auth.listUsers()) arr.push_back(toJson(u));
            sendJson(res, {{"items", arr}});
        });
    });

    svr.Post("/api/v1/users", [deps, requireAdmin, userInput](const Req& req, Res& res) {
        guarded(res, [&] {
            User actor = requireAdmin(req);
            sendJson(res, toJson(deps->auth.createUser(userInput(parseBody(req)), actor)), 201);
        });
    });

    svr.Put(R"(/api/v1/users/(\d+))", [deps, requireAdmin, userInput](const Req& req, Res& res) {
        guarded(res, [&] {
            User actor = requireAdmin(req);
            sendJson(res, toJson(deps->auth.updateUser(idParam(req), userInput(parseBody(req)), actor)));
        });
    });

    svr.Delete(R"(/api/v1/users/(\d+))", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User actor = requireAdmin(req);
            deps->auth.deleteUser(idParam(req), actor);
            res.status = 204;
        });
    });

    // ------------------------------------------------------------ drivers
    svr.Get("/api/v1/drivers", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireUser(req);
            json arr = json::array();
            for (const auto& x : deps->fleet.listDrivers()) arr.push_back(toJson(x, u.isAdmin()));
            sendJson(res, {{"items", arr}});
        });
    });

    svr.Post("/api/v1/drivers", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            sendJson(res, toJson(deps->fleet.createDriver(driverFromJson(parseBody(req)), u.username), true), 201);
        });
    });

    svr.Get(R"(/api/v1/drivers/(\d+))", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireUser(req);
            sendJson(res, toJson(deps->fleet.getDriver(idParam(req)), u.isAdmin()));
        });
    });

    svr.Put(R"(/api/v1/drivers/(\d+))", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            Driver current = deps->fleet.getDriver(idParam(req));
            sendJson(res, toJson(deps->fleet.updateDriver(driverFromJson(parseBody(req), current), u.username), true));
        });
    });

    svr.Delete(R"(/api/v1/drivers/(\d+))", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            deps->fleet.deleteDriver(idParam(req), u.username);
            res.status = 204;
        });
    });

    svr.Post(R"(/api/v1/drivers/(\d+)/photos)", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            std::vector<std::pair<std::string, std::string>> photos;
            for (const auto& f : req.form.get_files("photos")) photos.emplace_back(f.filename, f.content);
            auto outcomes = deps->fleet.enrollPhotos(idParam(req), photos, u.username);
            json arr = json::array();
            int enrolled = 0;
            for (const auto& o : outcomes) {
                arr.push_back({{"filename", o.filename}, {"enrolled", o.enrolled},
                               {"error", o.error.empty() ? json(nullptr) : json(o.error)}});
                enrolled += o.enrolled;
            }
            sendJson(res, {{"enrolled", enrolled}, {"photos", arr},
                           {"driver", toJson(deps->fleet.getDriver(idParam(req)), true)}});
        });
    });

    svr.Delete(R"(/api/v1/drivers/(\d+)/photos)", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            deps->fleet.clearPhotos(idParam(req), u.username);
            res.status = 204;
        });
    });

    // Profile picture: a display thumbnail, unrelated to the face-enrollment photos
    // above (those are only ever embedded, never stored). Admin-only for now, both
    // to manage and to view.
    svr.Get(R"(/api/v1/drivers/(\d+)/photo)", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            requireAdmin(req);
            Driver d = deps->fleet.getDriver(idParam(req));
            const std::string path = deps->cfg.portraitsDir() + "/" + d.photoFile;
            if (d.photoFile.empty() || !fs::exists(path)) throw NotFoundError("no photo");
            const std::string ext = fs::path(d.photoFile).extension().string();
            res.set_header("Cache-Control", "public, max-age=86400");
            res.set_header("X-Content-Type-Options", "nosniff");
            res.set_file_content(path, ext == ".png" ? "image/png" : ext == ".webp" ? "image/webp" : "image/jpeg");
        });
    });

    svr.Put(R"(/api/v1/drivers/(\d+)/photo)", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            if (!req.is_multipart_form_data() || !req.form.has_file("photo"))
                throw ValidationError("expected multipart/form-data with a 'photo' file");
            const auto file = req.form.get_file("photo");
            if (file.content.size() > (2u << 20)) throw ValidationError("the photo must be smaller than 2 MB");
            const char* ext = imageExtension(file.content);
            if (!ext) throw ValidationError("the photo must be a PNG, JPEG or WebP image");
            const int64_t id = idParam(req);
            Driver d = deps->fleet.getDriver(id);
            std::error_code ec;
            fs::create_directories(deps->cfg.portraitsDir(), ec);
            if (!d.photoFile.empty()) fs::remove(deps->cfg.portraitsDir() + "/" + d.photoFile, ec);
            const std::string filename = std::to_string(id) + ext;
            std::ofstream out(deps->cfg.portraitsDir() + "/" + filename, std::ios::binary | std::ios::trunc);
            out.write(file.content.data(), static_cast<std::streamsize>(file.content.size()));
            if (!out) throw UnavailableError("could not store the photo");
            out.close();
            sendJson(res, toJson(deps->fleet.setDriverPhoto(id, filename, u.username), true));
        });
    });

    svr.Delete(R"(/api/v1/drivers/(\d+)/photo)", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            const int64_t id = idParam(req);
            Driver d = deps->fleet.getDriver(id);
            std::error_code ec;
            if (!d.photoFile.empty()) fs::remove(deps->cfg.portraitsDir() + "/" + d.photoFile, ec);
            sendJson(res, toJson(deps->fleet.setDriverPhoto(id, "", u.username), true));
        });
    });

    svr.Put(R"(/api/v1/drivers/(\d+)/vehicles/(\d+))", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            deps->fleet.assign(idParam(req, 1), idParam(req, 2), u.username);
            sendJson(res, toJson(deps->fleet.getDriver(idParam(req, 1)), true));
        });
    });

    svr.Delete(R"(/api/v1/drivers/(\d+)/vehicles/(\d+))", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            deps->fleet.unassign(idParam(req, 1), idParam(req, 2), u.username);
            res.status = 204;
        });
    });

    // ------------------------------------------------------------ vehicles
    svr.Get("/api/v1/vehicles", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireUser(req);
            json arr = json::array();
            for (const auto& x : deps->fleet.listVehicles()) arr.push_back(toJson(x, u.isAdmin()));
            sendJson(res, {{"items", arr}});
        });
    });

    // Every new vehicle must have an owner: it's created together with the vehicle
    // (atomically), never added afterwards through this endpoint.
    svr.Post("/api/v1/vehicles", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            json b = parseBody(req);
            if (!b.contains("owner") || !b["owner"].is_object())
                throw ValidationError("owner is required when registering a new vehicle");
            auto vehicle = deps->fleet.createVehicleWithOwner(vehicleFromJson(b), driverFromJson(b["owner"]), u.username);
            sendJson(res, toJson(vehicle, true), 201);
        });
    });

    svr.Get(R"(/api/v1/vehicles/(\d+))", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireUser(req);
            sendJson(res, toJson(deps->fleet.getVehicle(idParam(req)), u.isAdmin()));
        });
    });

    svr.Put(R"(/api/v1/vehicles/(\d+))", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            Vehicle current = deps->fleet.getVehicle(idParam(req));
            sendJson(res, toJson(deps->fleet.updateVehicle(vehicleFromJson(parseBody(req), current), u.username), true));
        });
    });

    svr.Delete(R"(/api/v1/vehicles/(\d+))", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            deps->fleet.deleteVehicle(idParam(req), u.username);
            res.status = 204;
        });
    });

    // Backfills an owner (+ authorization code) onto a legacy vehicle that predates
    // this feature. Rejects if the vehicle already has one.
    svr.Put(R"(/api/v1/vehicles/(\d+)/owner)", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            auto vehicle = deps->fleet.setVehicleOwner(idParam(req), driverFromJson(parseBody(req)), u.username);
            sendJson(res, toJson(vehicle, true));
        });
    });

    // Redeems an owner's authorization code to add a new authorized (non-owner) driver
    // to their vehicle, without needing the owner present. Wrong-code attempts are
    // rate-limited the same way as the setup code.
    svr.Post("/api/v1/drivers/authorize", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            authCodeLimiter().guard();
            json b = parseBody(req);
            Vehicle v;
            try {
                v = deps->fleet.findVehicleByAuthCode(b.value("code", ""));
            } catch (const NotFoundError&) {
                authCodeLimiter().fail();
                throw ValidationError("authorization code not recognized");
            }
            sendJson(res, toJson(deps->fleet.authorizeDriverForVehicle(v.id, driverFromJson(b), u.username), true), 201);
        });
    });

    // ------------------------------------------------------------ cameras
    auto cameraJson = [deps](const Camera& c) {
        json j = toJson(c);
        j["status"] = deps->cameras.status(c.id);
        return j;
    };

    svr.Get("/api/v1/cameras", [deps, requireUser, cameraJson](const Req& req, Res& res) {
        guarded(res, [&] {
            requireUser(req);
            json arr = json::array();
            for (const auto& c : deps->cameras.list()) arr.push_back(cameraJson(c));
            sendJson(res, {{"items", arr}});
        });
    });

    svr.Post("/api/v1/cameras", [deps, requireAdmin, cameraJson](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            Camera c;
            c.sampleFps = deps->cfg.inference.sampleFps;
            sendJson(res, cameraJson(deps->cameras.create(cameraFromJson(parseBody(req), c), u.username)), 201);
        });
    });

    // Tries a source without saving it: {ok, backend, width, height, fps} or {ok: false, error}.
    svr.Post("/api/v1/cameras/probe", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            requireAdmin(req);
            sendJson(res, deps->cameras.probe(parseBody(req).value("source", "")));
        });
    });

    svr.Get(R"(/api/v1/cameras/(\d+))", [deps, requireUser, cameraJson](const Req& req, Res& res) {
        guarded(res, [&] {
            requireUser(req);
            sendJson(res, cameraJson(deps->cameras.get(idParam(req))));
        });
    });

    svr.Put(R"(/api/v1/cameras/(\d+))", [deps, requireAdmin, cameraJson](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            Camera current = deps->cameras.get(idParam(req));
            sendJson(res, cameraJson(deps->cameras.update(cameraFromJson(parseBody(req), current), u.username)));
        });
    });

    svr.Delete(R"(/api/v1/cameras/(\d+))", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            deps->cameras.remove(idParam(req), u.username);
            res.status = 204;
        });
    });

    // One current frame.
    svr.Get(R"(/api/v1/cameras/(\d+)/preview\.jpg)", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            requireUser(req);
            auto p = deps->cameras.preview(idParam(req), 0);
            if (!p) throw UnavailableError("no picture from this camera right now");
            res.set_header("Cache-Control", "no-store");
            res.set_content(std::move(p->second), "image/jpeg");
        });
    });

    // Live view for <img src>: MJPEG (multipart/x-mixed-replace), ~10 fps.
    svr.Get(R"(/api/v1/cameras/(\d+)/live\.mjpeg)", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            requireUser(req);
            const int64_t id = idParam(req);
            if (!deps->cameras.get(id).enabled) throw ConflictError("camera is disabled");
            struct State { uint64_t last = 0; int misses = 0; };
            auto st = std::make_shared<State>();
            res.set_header("Cache-Control", "no-cache, no-store");
            res.set_chunked_content_provider("multipart/x-mixed-replace; boundary=guardframe",
                [deps, id, st](std::size_t, httplib::DataSink& sink) {
                    std::optional<std::pair<uint64_t, std::string>> p;
                    try { p = deps->cameras.preview(id, st->last); }
                    catch (...) { return false; }   // camera deleted or disabled
                    if (!p) {
                        // Nothing new for a while: inference may have restarted (sequence reset).
                        if (++st->misses >= 2) st->last = 0;
                        return sink.is_writable();
                    }
                    st->misses = 0;
                    st->last = p->first;
                    const std::string head = "--guardframe\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                                             std::to_string(p->second.size()) + "\r\n\r\n";
                    return sink.write(head.data(), head.size()) && sink.write(p->second.data(), p->second.size()) &&
                           sink.write("\r\n", 2);
                });
        });
    });

    // ------------------------------------------------------------ jobs
    // Multipart field "video", streamed straight to disk (videos can be large).
    svr.Post("/api/v1/jobs", [deps](const Req& req, Res& res, const httplib::ContentReader& reader) {
        guarded(res, [&] {
            auto user = deps->auth.authenticate(bearerToken(req));
            if (!user) throw UnauthorizedError("login required");
            if (!user->isAdmin()) throw ForbiddenError("testing with videos or pictures needs an administrator account");
            if (!req.is_multipart_form_data()) throw ValidationError("expected multipart/form-data with a 'video' file");

            const std::string jobId = deps->jobs.newJobId();
            const std::size_t maxBytes = deps->cfg.server.maxUploadMb << 20;
            std::string path, originalName;
            std::ofstream out;
            std::size_t written = 0;
            bool inVideo = false, tooLarge = false;

            reader(
                [&](const httplib::FormData& part) {
                    inVideo = part.name == "video" && !part.filename.empty() && path.empty();
                    if (inVideo) {
                        originalName = fs::path(part.filename).filename().string();
                        path = deps->jobs.mediaPathFor(jobId, originalName);
                        out.open(path, std::ios::binary);
                    }
                    return true;
                },
                [&](const char* data, std::size_t len) {
                    if (!inVideo) return true;
                    written += len;
                    if (written > maxBytes) { tooLarge = true; return false; }
                    out.write(data, static_cast<std::streamsize>(len));
                    return static_cast<bool>(out);
                });
            out.close();

            std::error_code ec;
            if (tooLarge) {
                fs::remove(path, ec);
                return sendError(res, 413, "too_large",
                                 "video exceeds " + std::to_string(deps->cfg.server.maxUploadMb) + " MB");
            }
            if (path.empty() || written == 0) {
                if (!path.empty()) fs::remove(path, ec);
                throw ValidationError("multipart field 'video' with a file is required");
            }
            Job job = deps->jobs.createUpload(jobId, path, originalName, static_cast<int64_t>(written), user->username);
            sendJson(res, toJson(job, deps->jobs.queuePosition(job)), 202);
        });
    });

    svr.Get("/api/v1/jobs", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            requireAdmin(req);
            json arr = json::array();
            for (const auto& j : deps->jobs.list(intParam(req, "limit", 50, 500), intParam(req, "offset", 0, 1 << 30)))
                arr.push_back(toJson(j, deps->jobs.queuePosition(j)));
            sendJson(res, {{"items", arr}});
        });
    });

    svr.Get(R"(/api/v1/jobs/([0-9a-f]+))", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            requireAdmin(req);
            Job j = deps->jobs.get(req.matches[1]);
            sendJson(res, toJson(j, deps->jobs.queuePosition(j)));
        });
    });

    svr.Post(R"(/api/v1/jobs/([0-9a-f]+)/cancel)", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            deps->jobs.cancel(req.matches[1], u.username);
            sendJson(res, toJson(deps->jobs.get(req.matches[1])), 202);
        });
    });

    svr.Delete(R"(/api/v1/jobs/([0-9a-f]+))", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireAdmin(req);
            deps->jobs.remove(req.matches[1], u.username);
            res.status = 204;
        });
    });

    svr.Get(R"(/api/v1/jobs/([0-9a-f]+)/video)", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            requireAdmin(req);
            Job j = deps->jobs.get(req.matches[1]);
            if (!fs::exists(j.source)) throw NotFoundError("video file no longer exists");
            res.set_file_content(j.source, contentTypeFor(j.source));
        });
    });

    svr.Get(R"(/api/v1/jobs/([0-9a-f]+)/frames)", [deps, requireAdmin](const Req& req, Res& res) {
        guarded(res, [&] {
            requireAdmin(req);
            const std::string path = deps->jobs.framesPath(deps->jobs.get(req.matches[1]).id);
            if (!fs::exists(path)) throw NotFoundError("no frame data for this job (yet)");
            res.set_file_content(path, "application/json");
        });
    });

    // ------------------------------------------------------------ events & alerts
    svr.Get("/api/v1/access-events", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireUser(req);
            AccessEventFilter f;
            if (req.has_param("camera_id")) {
                try { f.cameraId = std::stoll(req.get_param_value("camera_id")); }
                catch (...) { throw ValidationError("invalid camera_id"); }
            }
            f.source = req.get_param_value("source");
            if (!f.source.empty() && f.source != "camera" && f.source != "video")
                throw ValidationError("source must be camera or video");
            if (!u.isAdmin()) f.source = "camera";   // test videos are an administrator tool
            f.verdict = req.get_param_value("verdict");
            json arr = json::array();
            for (const auto& e : deps->events.listAccessEvents(intParam(req, "limit", 50, 500),
                                                              intParam(req, "offset", 0, 1 << 30), f))
                arr.push_back(toJson(e));
            sendJson(res, {{"items", arr}});
        });
    });

    svr.Get(R"(/api/v1/access-events/(\d+))", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireUser(req);
            auto e = deps->events.getAccessEvent(idParam(req));
            if (!e || (!u.isAdmin() && !e->fromCamera())) throw NotFoundError("access event not found");
            sendJson(res, toJson(*e));
        });
    });

    svr.Get(R"(/api/v1/access-events/(\d+)/snapshot\.jpg)", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireUser(req);
            auto e = deps->events.getAccessEvent(idParam(req));
            if (!e || (!u.isAdmin() && !e->fromCamera())) throw NotFoundError("access event not found");
            const std::string path = deps->cameras.snapshotFile(*e);
            if (path.empty() || !fs::exists(path)) throw NotFoundError("no snapshot for this event");
            res.set_header("Cache-Control", "private, max-age=86400");
            res.set_file_content(path, "image/jpeg");
        });
    });

    svr.Get("/api/v1/alerts", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            requireUser(req);
            const bool openOnly = req.get_param_value("open") == "true";
            json arr = json::array();
            for (const auto& a : deps->events.listAlerts(openOnly, intParam(req, "limit", 100, 500)))
                arr.push_back(toJson(a));
            sendJson(res, {{"items", arr}});
        });
    });

    svr.Post(R"(/api/v1/alerts/(\d+)/(acknowledge|resolve))", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            User u = requireUser(req);
            const int64_t id = idParam(req);
            const bool ok = req.matches[2] == "resolve" ? deps->events.resolve(id, u.username)
                                                        : deps->events.acknowledge(id, u.username);
            if (!ok) throw NotFoundError("alert not found");
            deps->events.audit(u.username, req.matches[2], "alert", std::to_string(id));
            auto a = *deps->events.getAlert(id);
            deps->bus.publish("alert.updated", toJson(a));
            sendJson(res, toJson(a));
        });
    });

    // Server-Sent Events (guards do not receive test-video events). ?job=<id> limits the stream to one job and replays
    // what is still buffered for it; ?camera=<id> to one camera's live
    // updates; Last-Event-ID resumes after a drop.
    svr.Get("/api/v1/events", [deps, requireUser](const Req& req, Res& res) {
        guarded(res, [&] {
            const bool admin = requireUser(req).isAdmin();
            std::string job = req.get_param_value("job");
            uint64_t after = job.empty() ? deps->bus.lastId() : 0;
            if (job.empty() && req.has_param("camera")) job = "camera:" + req.get_param_value("camera");
            try {
                if (req.has_header("Last-Event-ID")) after = std::stoull(req.get_header_value("Last-Event-ID"));
                else if (req.has_param("last_event_id")) after = std::stoull(req.get_param_value("last_event_id"));
            } catch (...) { throw ValidationError("invalid Last-Event-ID"); }

            auto cursor = std::make_shared<uint64_t>(after);
            res.set_header("Cache-Control", "no-cache");
            res.set_header("X-Accel-Buffering", "no");
            res.set_chunked_content_provider("text/event-stream",
                [deps, job, cursor, admin](std::size_t, httplib::DataSink& sink) {
                    auto evs = deps->bus.waitAfter(*cursor, std::chrono::seconds(15), job);
                    if (!admin)   // test videos are an administrator tool
                        evs.erase(std::remove_if(evs.begin(), evs.end(), [&](const BusEvent& e) {
                            const bool testVideo = e.type.rfind("job.", 0) == 0 ||
                                (e.type == "access.event" && e.json.find("\"source\":\"video\"") != std::string::npos);
                            if (!testVideo) return false;
                            *cursor = std::max(*cursor, e.id);
                            return true;
                        }), evs.end());
                    if (evs.empty()) {
                        static const std::string ping = ": ping\n\n";
                        return sink.write(ping.data(), ping.size());
                    }
                    std::string chunk;
                    for (const auto& e : evs) {
                        chunk += "id: " + std::to_string(e.id) + "\ndata: " + e.json + "\n\n";
                        *cursor = e.id;
                    }
                    return sink.write(chunk.data(), chunk.size());
                });
        });
    });

    // ------------------------------------------------------------ admin dashboard
    struct SizeCache { std::mutex m; uintmax_t bytes = 0; std::chrono::steady_clock::time_point at{}; };
    auto snapshotSize = std::make_shared<SizeCache>();

    svr.Get("/api/v1/dashboard", [deps, requireAdmin, started, snapshotSize](const Req& req, Res& res) {
        guarded(res, [&] {
            requireAdmin(req);
            StatsRepository& st = deps->stats;
            const auto hours = timeBuckets(24, true);
            const auto days = timeBuckets(14, false);
            json hourly = bucketSeries(hours, st.hourly(25));
            json daily = bucketSeries(days, st.daily(14));
            json today = daily.back(), yesterday = daily[daily.size() - 2];

            std::map<int64_t, int> camToday;
            for (const auto& [id, n] : st.visitsTodayByCamera()) camToday[id] = n;
            json cameras = json::array();
            int enabled = 0, live = 0;
            for (const auto& c : deps->cameras.list()) {
                json j = toJson(c);
                j["status"] = deps->cameras.status(c.id);
                j["visits_today"] = camToday.count(c.id) ? camToday[c.id] : 0;
                if (c.enabled) { ++enabled; if (j["status"]["state"] == "live") ++live; }
                cameras.push_back(j);
            }

            json plates = json::array();
            for (const auto& p : st.unregisteredPlates(7, 8))
                plates.push_back({{"plate", p.plate}, {"count", p.count}, {"last_seen", isoTime(p.lastSeen)},
                                  {"camera", p.camera.empty() ? json(nullptr) : json(p.camera)}});
            json drivers = json::array();
            for (const auto& d : st.topDrivers(7, 6))
                drivers.push_back({{"driver_id", d.driverId}, {"name", d.name}, {"visits", d.visits}, {"authorized", d.authorized}});

            AlertStats a = st.alerts();
            json median = nullptr;
            if (!a.responseSeconds.empty()) {
                auto& v = a.responseSeconds;
                std::sort(v.begin(), v.end());
                median = v.size() % 2 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2;
            }
            json openAlerts = json::array();
            for (const auto& al : deps->events.listAlerts(true, 5)) openAlerts.push_back(toJson(al));

            uintmax_t snapBytes;
            {
                std::lock_guard<std::mutex> lk(snapshotSize->m);
                if (std::chrono::steady_clock::now() - snapshotSize->at > std::chrono::minutes(5)) {
                    snapshotSize->bytes = directorySize(deps->cfg.snapshotsDir());
                    snapshotSize->at = std::chrono::steady_clock::now();
                }
                snapBytes = snapshotSize->bytes;
            }
            std::error_code ec;
            uintmax_t dbBytes = 0;
            for (const char* suffix : {"", "-wal"}) {
                auto n = fs::file_size(deps->cfg.dbPath() + suffix, ec);
                if (!ec) dbBytes += n;
            }
            auto space = fs::space(deps->cfg.dataDir(), ec);
            const auto inf = deps->inference.health();
            const auto [admins, guards] = st.activeUsers();
            auto up = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started);

            sendJson(res, {
                {"today", today}, {"yesterday", yesterday},
                {"hourly", hourly}, {"daily", daily},
                {"cameras", cameras}, {"cameras_enabled", enabled}, {"cameras_live", live},
                {"unregistered_plates", plates}, {"top_drivers", drivers},
                {"alerts", {{"open", a.open}, {"open_critical", a.openCritical}, {"unacknowledged", a.unacknowledged},
                            {"median_response_s", median}, {"responses_7d", a.responseSeconds.size()},
                            {"recent_open", openAlerts}}},
                {"system", {{"version", GUARD_VERSION}, {"uptime_s", up.count()},
                            {"inference", {{"status", inf.status}, {"message", inf.message}}},
                            {"disk_free_bytes", ec ? 0 : space.available}, {"disk_total_bytes", ec ? 0 : space.capacity},
                            {"snapshot_bytes", snapBytes}, {"database_bytes", dbBytes},
                            {"snapshot_retention_days", deps->cfg.live.snapshotRetentionDays},
                            {"active_admins", admins}, {"active_guards", guards}}}});
        });
    });

    // ------------------------------------------------------------ frontend
    const std::string ui = deps->cfg.paths.frontendDir;
    if (!ui.empty() && fs::is_directory(ui) && svr.set_mount_point("/", ui)) {
        LOG(Info) << "Serving frontend from " << ui;
    } else if (!ui.empty()) {
        LOG(Warn) << "Frontend directory not found: " << ui << " (API only)";
    }
    svr.set_error_handler([](const Req& req, Res& res) {
        if (res.status == 404 && req.path.rfind("/api/", 0) == 0 && res.body.empty())
            sendError(res, 404, "not_found", "no such endpoint: " + req.method + " " + req.path);
    });
}

} // namespace guard
