// ---------------------------------------------------------------------------
// guard-inference — the GPU perception service (internal, loopback only).
//
// Knows nothing about drivers, vehicles, verdicts, users or UI. Only
// guard-core talks to it:
//   GET    /v1/health                  state of models + queue (no auth)
//   GET    /v1/models                  model/engine details
//   POST   /v1/jobs                    {job_id, source, sample_fps?} -> queued
//   GET    /v1/jobs/{id}               status
//   GET    /v1/jobs/{id}/stream        SSE: start, frame..., done|error|cancelled
//                                      (resume with Last-Event-ID or ?from=N)
//   DELETE /v1/jobs/{id}               cancel
//   POST   /v1/embed                   multipart images -> face embeddings
//   PUT    /v1/streams/{id}            {source, sample_fps?} start/replace a live camera stream
//   GET    /v1/streams                 all live streams with state and rates
//   GET    /v1/streams/{id}            one stream
//   DELETE /v1/streams/{id}            stop
//   GET    /v1/streams/{id}/events     SSE: status, frame... (resume with Last-Event-ID or ?from=N)
//   GET    /v1/streams/{id}/preview.jpg?after=N   newest preview JPEG (long-poll), X-Frame-Seq
//   GET    /v1/streams/{id}/frames/{n}.jpg        analysed frame n (only frames with detections)
//   POST   /v1/probe                   {source} -> can it be opened? resolution, fps
// Auth: X-Guard-Secret must equal server.secret / GUARD_INFERENCE_SECRET.
// ---------------------------------------------------------------------------
#include "ai/base64.h"
#include "ai/config.h"
#include "ai/image_io.h"
#include "ai/job_queue.h"
#include "ai/live_streams.h"
#include "ai/logging.h"
#include "ai/model_manager.h"
#include "ai/perception.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <regex>

using json = nlohmann::json;

namespace {

void sendError(httplib::Response& res, int status, const std::string& code, const std::string& detail) {
    res.status = status;
    res.set_content(json{{"error", code}, {"detail", detail}}.dump(), "application/json");
}

void sendJson(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

bool validJobId(const std::string& id) {
    static const std::regex re("^[A-Za-z0-9_-]{1,64}$");
    return std::regex_match(id, re);
}

} // namespace

int main(int argc, char** argv) {
    const std::string configPath = argc > 1 ? argv[1] : "config/inference.json";

    ai::AppConfig cfg;
    std::string err;
    if (!ai::loadConfig(configPath, cfg, err)) {
        LOG_ERROR << err;
        return 1;
    }
    ai::Logger::instance().setLevel(ai::logLevelFromString(cfg.logging.level));
    ai::Logger::instance().setJson(cfg.logging.jsonLogs);
    if (cfg.server.secret.empty())
        LOG_WARN << "No shared secret configured: any local process can use this service";

    ai::ModelManager models;
    models.loadAsync(cfg);
    ai::Perception perception(models);
    ai::JobQueue queue(perception, cfg.video);
    ai::StreamManager streams(perception, cfg.live);
    const auto started = std::chrono::steady_clock::now();

    httplib::Server svr;
    // Each live stream holds one event connection from core, and each viewer a
    // preview long-poll: size the pool so they never starve ordinary requests.
    svr.new_task_queue = [&cfg] {
        return new httplib::ThreadPool(std::max(cfg.server.threads, 8 + 3 * cfg.live.maxStreams));
    };
    svr.set_payload_max_length(cfg.server.maxRequestBytes);
    svr.set_read_timeout(cfg.server.timeoutS, 0);

    const std::string secret = cfg.server.secret;
    svr.set_pre_routing_handler([secret](const httplib::Request& req, httplib::Response& res) {
        if (req.path == "/v1/health" || secret.empty()) return httplib::Server::HandlerResponse::Unhandled;
        if (req.get_header_value("X-Guard-Secret") == secret) return httplib::Server::HandlerResponse::Unhandled;
        sendError(res, 401, "unauthorized", "missing or wrong X-Guard-Secret");
        return httplib::Server::HandlerResponse::Handled;
    });

    svr.Get("/v1/health", [&](const httplib::Request&, httplib::Response& res) {
        static const char* states[] = {"loading", "ok", "error"};
        auto up = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started);
        sendJson(res, {{"status", states[static_cast<int>(models.state())]},
                       {"service", "guard-inference"},
                       {"message", models.message()},
                       {"queue", queue.stats()},
                       {"streams", streams.count()},
                       {"uptime_s", up.count()}});
    });

    svr.Get("/v1/models", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(models.statusJson(), "application/json");
    });

    svr.Post("/v1/jobs", [&](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { return sendError(res, 400, "bad_request", "invalid JSON body"); }
        const std::string id = body.value("job_id", "");
        const std::string source = body.value("source", "");
        if (!validJobId(id)) return sendError(res, 400, "bad_request", "job_id must match [A-Za-z0-9_-]{1,64}");
        if (source.empty()) return sendError(res, 400, "bad_request", "source is required");
        if (!models.ready()) return sendError(res, 503, "models_not_ready", models.message());
        try {
            auto job = queue.submit(id, source, body.value("sample_fps", 0.0));
            sendJson(res, {{"job_id", job->id()}, {"status", job->status()}}, 202);
        } catch (const std::invalid_argument& e) {
            sendError(res, 409, "conflict", e.what());
        }
    });

    svr.Get(R"(/v1/jobs/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        auto job = queue.find(req.matches[1]);
        if (!job) return sendError(res, 404, "not_found", "unknown job");
        sendJson(res, {{"job_id", job->id()}, {"status", job->status()}, {"source", job->source()}});
    });

    svr.Delete(R"(/v1/jobs/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!queue.cancel(req.matches[1])) return sendError(res, 404, "not_found", "unknown job");
        sendJson(res, {{"cancelled", true}});
    });

    svr.Get(R"(/v1/jobs/([A-Za-z0-9_-]+)/stream)", [&](const httplib::Request& req, httplib::Response& res) {
        auto job = queue.find(req.matches[1]);
        if (!job) return sendError(res, 404, "not_found", "unknown job");

        std::size_t from = 0;
        try {
            if (req.has_header("Last-Event-ID")) from = std::stoul(req.get_header_value("Last-Event-ID")) + 1;
            else if (req.has_param("from")) from = std::stoul(req.get_param_value("from"));
        } catch (...) { return sendError(res, 400, "bad_request", "invalid resume position"); }

        auto next = std::make_shared<std::size_t>(from);
        res.set_header("Cache-Control", "no-cache");
        res.set_chunked_content_provider("text/event-stream",
            [job, next](std::size_t, httplib::DataSink& sink) {
                bool finished = false;
                std::string ev = job->waitEvent(*next, std::chrono::seconds(15), finished);
                if (!ev.empty()) {
                    std::string chunk = "id: " + std::to_string(*next) + "\ndata: " + ev + "\n\n";
                    ++*next;
                    return sink.write(chunk.data(), chunk.size());
                }
                if (finished) { sink.done(); return true; }
                static const std::string ping = ": ping\n\n";   // keeps idle connections alive
                return sink.write(ping.data(), ping.size());
            });
    });

    svr.Post("/v1/embed", [&](const httplib::Request& req, httplib::Response& res) {
        auto files = req.form.get_files("images");
        if (files.empty()) return sendError(res, 400, "bad_request", "multipart field 'images' is required");
        if (!models.ready()) return sendError(res, 503, "models_not_ready", models.message());

        json results = json::array();
        for (std::size_t i = 0; i < files.size(); ++i) {
            std::vector<char> bytes(files[i].content.begin(), files[i].content.end());
            auto mat = ai::decodeImageBytes(bytes);
            json r{{"index", i}};
            if (!mat) { r["error"] = "invalid_image"; results.push_back(r); continue; }
            auto face = perception.bestFace(*mat);
            if (!face) { r["error"] = "no_face"; results.push_back(r); continue; }
            r["embedding"] = ai::embedToBase64(face->embedding);
            r["dim"] = face->embedding.size();
            r["conf"] = face->confidence;
            r["bbox"] = json::array({face->bbox.x, face->bbox.y, face->bbox.w, face->bbox.h});
            results.push_back(r);
        }
        sendJson(res, {{"results", results}});
    });

    // ------------------------------------------------------------ live streams
    svr.Put(R"(/v1/streams/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { return sendError(res, 400, "bad_request", "invalid JSON body"); }
        const std::string id = req.matches[1];
        const std::string source = body.value("source", "");
        if (!validJobId(id)) return sendError(res, 400, "bad_request", "stream id must match [A-Za-z0-9_-]{1,64}");
        if (source.empty()) return sendError(res, 400, "bad_request", "source is required");
        if (!models.ready()) return sendError(res, 503, "models_not_ready", models.message());
        double fps = body.value("sample_fps", cfg.video.sampleFps);
        if (!(fps > 0 && fps <= 30)) fps = cfg.video.sampleFps;
        try {
            auto [stream, created] = streams.ensure(id, source, fps);
            sendJson(res, stream->info(), created ? 201 : 200);
        } catch (const std::length_error& e) {
            sendError(res, 409, "too_many_streams", e.what());
        }
    });

    svr.Get("/v1/streams", [&](const httplib::Request&, httplib::Response& res) {
        sendJson(res, {{"items", streams.list()}});
    });

    svr.Get(R"(/v1/streams/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        auto s = streams.find(req.matches[1]);
        if (!s) return sendError(res, 404, "not_found", "unknown stream");
        sendJson(res, s->info());
    });

    svr.Delete(R"(/v1/streams/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!streams.remove(req.matches[1])) return sendError(res, 404, "not_found", "unknown stream");
        sendJson(res, {{"removed", true}});
    });

    svr.Get(R"(/v1/streams/([A-Za-z0-9_-]+)/events)", [&](const httplib::Request& req, httplib::Response& res) {
        auto s = streams.find(req.matches[1]);
        if (!s) return sendError(res, 404, "not_found", "unknown stream");
        uint64_t from = s->info().value("next_seq", 0ull);   // default: from now on
        try {
            if (req.has_header("Last-Event-ID")) from = std::stoull(req.get_header_value("Last-Event-ID")) + 1;
            else if (req.has_param("from")) from = std::stoull(req.get_param_value("from"));
        } catch (...) { return sendError(res, 400, "bad_request", "invalid resume position"); }

        auto next = std::make_shared<uint64_t>(from);
        std::weak_ptr<ai::LiveStream> weak = s;
        res.set_header("Cache-Control", "no-cache");
        res.set_chunked_content_provider("text/event-stream",
            [weak, next](std::size_t, httplib::DataSink& sink) {
                auto stream = weak.lock();
                if (!stream) { sink.done(); return true; }     // stream removed
                bool stopped = false;
                // Short keep-alive: lets core notice shutdown/removal quickly.
                std::string ev = stream->waitEvent(*next, std::chrono::seconds(2), stopped);
                if (!ev.empty()) {
                    std::string chunk = "id: " + std::to_string(*next - 1) + "\ndata: " + ev + "\n\n";
                    return sink.write(chunk.data(), chunk.size());
                }
                if (stopped) { sink.done(); return true; }
                static const std::string ping = ": ping\n\n";
                return sink.write(ping.data(), ping.size());
            });
    });

    svr.Get(R"(/v1/streams/([A-Za-z0-9_-]+)/preview.jpg)", [&](const httplib::Request& req, httplib::Response& res) {
        auto s = streams.find(req.matches[1]);
        if (!s) return sendError(res, 404, "not_found", "unknown stream");
        uint64_t after = 0;
        try { if (req.has_param("after")) after = std::stoull(req.get_param_value("after")); }
        catch (...) { return sendError(res, 400, "bad_request", "invalid after"); }
        auto p = s->preview(after, std::chrono::seconds(3));
        if (!p) { res.status = 204; return; }
        res.set_header("X-Frame-Seq", std::to_string(p->first));
        res.set_header("Cache-Control", "no-store");
        res.set_content(std::move(p->second), "image/jpeg");
    });

    svr.Get(R"(/v1/streams/([A-Za-z0-9_-]+)/frames/(\d+)\.jpg)", [&](const httplib::Request& req, httplib::Response& res) {
        auto s = streams.find(req.matches[1]);
        if (!s) return sendError(res, 404, "not_found", "unknown stream");
        auto jpeg = s->frameImage(std::stoull(req.matches[2].str()));
        if (!jpeg) return sendError(res, 404, "not_found", "frame image no longer kept");
        res.set_content(std::move(*jpeg), "image/jpeg");
    });

    svr.Post("/v1/probe", [&](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { return sendError(res, 400, "bad_request", "invalid JSON body"); }
        const std::string source = body.value("source", "");
        if (source.empty()) return sendError(res, 400, "bad_request", "source is required");
        sendJson(res, ai::StreamManager::probe(source));
    });

    LOG_INFO << "guard-inference listening on " << cfg.server.host << ":" << cfg.server.port;
    if (!svr.listen(cfg.server.host, cfg.server.port)) {
        LOG_ERROR << "cannot listen on " << cfg.server.host << ":" << cfg.server.port;
        return 1;
    }
    ai::Logger::instance().shutdown();
    return 0;
}
