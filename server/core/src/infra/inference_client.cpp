#include "infra/inference_client.h"
#include "common/base64.h"
#include <httplib.h>

using json = nlohmann::json;

namespace guard {

namespace {

httplib::Headers authHeaders(const std::string& secret) {
    httplib::Headers h;
    if (!secret.empty()) h.emplace("X-Guard-Secret", secret);
    return h;
}

std::string errorDetail(const httplib::Result& r) {
    try {
        json j = json::parse(r->body);
        return j.value("detail", j.value("error", r->body));
    } catch (...) {
        return r->body;
    }
}

} // namespace

InferenceClient::InferenceClient(std::string baseUrl, std::string secret)
    : baseUrl_(std::move(baseUrl)), secret_(std::move(secret)) {}

InferenceHealth InferenceClient::health() const {
    httplib::Client cli(baseUrl_);
    cli.set_connection_timeout(1);
    cli.set_read_timeout(3);
    InferenceHealth h;
    auto r = cli.Get("/v1/health");
    if (!r) return h;
    h.reachable = true;
    try {
        json j = json::parse(r->body);
        h.status = j.value("status", "error");
        h.message = j.value("message", "");
    } catch (...) {
        h.status = "error";
        h.message = "invalid health response";
    }
    return h;
}

json InferenceClient::models() const {
    httplib::Client cli(baseUrl_);
    cli.set_connection_timeout(1);
    auto r = cli.Get("/v1/models", authHeaders(secret_));
    if (!r) throw InferenceUnavailable("inference unreachable at " + baseUrl_);
    return json::parse(r->body);
}

void InferenceClient::submitJob(const std::string& jobId, const std::string& source, double sampleFps) const {
    httplib::Client cli(baseUrl_);
    cli.set_connection_timeout(2);
    json body{{"job_id", jobId}, {"source", source}, {"sample_fps", sampleFps}};
    auto r = cli.Post("/v1/jobs", authHeaders(secret_), body.dump(), "application/json");
    if (!r) throw InferenceUnavailable("inference unreachable at " + baseUrl_);
    if (r->status != 202 && r->status != 409) throw InferenceError(r->status, errorDetail(r));
}

void InferenceClient::cancelJob(const std::string& jobId) const {
    httplib::Client cli(baseUrl_);
    cli.set_connection_timeout(1);
    cli.Delete("/v1/jobs/" + jobId, authHeaders(secret_));
}

InferenceClient::StreamEnd InferenceClient::stream(
    const std::string& jobId, int64_t lastSeq,
    const std::function<bool(const json&, int64_t)>& onEvent) const {
    return readSse("/v1/jobs/" + jobId + "/stream", lastSeq, onEvent);
}

InferenceClient::StreamEnd InferenceClient::streamEvents(
    const std::string& streamId, int64_t lastSeq,
    const std::function<bool(const json&, int64_t)>& onEvent, const std::atomic<bool>* abort) const {
    return readSse("/v1/streams/" + streamId + "/events", lastSeq, onEvent, abort);
}

InferenceClient::StreamEnd InferenceClient::readSse(
    const std::string& path, int64_t lastSeq,
    const std::function<bool(const json&, int64_t)>& onEvent, const std::atomic<bool>* abort) const {
    httplib::Client cli(baseUrl_);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(60);   // server pings every 15 s

    httplib::Headers headers = authHeaders(secret_);
    if (lastSeq >= 0) headers.emplace("Last-Event-ID", std::to_string(lastSeq));

    std::string buffer;
    bool stopped = false, finished = false;
    int status = 0;

    auto r = cli.Get(
        path, headers,
        [&](const httplib::Response& res) { status = res.status; return res.status == 200; },
        [&](const char* data, std::size_t len) {
            if (abort && *abort) { stopped = true; return false; }
            buffer.append(data, len);
            std::size_t end;
            while ((end = buffer.find("\n\n")) != std::string::npos) {
                std::string block = buffer.substr(0, end);
                buffer.erase(0, end + 2);
                int64_t seq = -1;
                std::string payload;
                std::size_t pos = 0;
                while (pos < block.size()) {
                    std::size_t nl = block.find('\n', pos);
                    std::string line = block.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                    pos = nl == std::string::npos ? block.size() : nl + 1;
                    if (line.rfind("id: ", 0) == 0) seq = std::stoll(line.substr(4));
                    else if (line.rfind("data: ", 0) == 0) payload += line.substr(6);
                }
                if (payload.empty()) continue;   // heartbeat
                json ev = json::parse(payload, nullptr, false);
                if (ev.is_discarded()) continue;
                const std::string type = ev.value("type", "");
                if (type == "done" || type == "error" || type == "cancelled") finished = true;
                if (!onEvent(ev, seq)) { stopped = true; return false; }
            }
            return true;
        });

    if (stopped) return StreamEnd::Stopped;
    if (status == 404) return StreamEnd::NotFound;
    if (finished) return StreamEnd::Finished;
    if (!r && status == 0) throw InferenceUnavailable("inference unreachable at " + baseUrl_);
    return StreamEnd::Disconnected;
}

std::vector<EmbedResult> InferenceClient::embed(const std::vector<std::string>& images) const {
    httplib::Client cli(baseUrl_);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(120);
    httplib::UploadFormDataItems items;
    for (std::size_t i = 0; i < images.size(); ++i)
        items.push_back({"images", images[i], "photo" + std::to_string(i), "application/octet-stream"});

    auto r = cli.Post("/v1/embed", authHeaders(secret_), items);
    if (!r) throw InferenceUnavailable("inference unreachable at " + baseUrl_);
    if (r->status != 200) throw InferenceError(r->status, errorDetail(r));

    std::vector<EmbedResult> out(images.size());
    const json doc = json::parse(r->body);   // must outlive the loop below
    for (const auto& item : doc.at("results")) {
        const std::size_t idx = item.value("index", std::size_t(0));
        if (idx >= out.size()) continue;
        if (item.contains("embedding")) out[idx].embedding = base64ToEmbed(item["embedding"].get<std::string>());
        else out[idx].error = item.value("error", "failed");
    }
    return out;
}

json InferenceClient::ensureStream(const std::string& streamId, const std::string& source, double sampleFps) const {
    httplib::Client cli(baseUrl_);
    cli.set_connection_timeout(2);
    json body{{"source", source}, {"sample_fps", sampleFps}};
    auto r = cli.Put("/v1/streams/" + streamId, authHeaders(secret_), body.dump(), "application/json");
    if (!r) throw InferenceUnavailable("inference unreachable at " + baseUrl_);
    if (r->status != 200 && r->status != 201) throw InferenceError(r->status, errorDetail(r));
    return json::parse(r->body);
}

void InferenceClient::removeStream(const std::string& streamId) const {
    httplib::Client cli(baseUrl_);
    cli.set_connection_timeout(1);
    cli.Delete("/v1/streams/" + streamId, authHeaders(secret_));
}

std::optional<std::pair<uint64_t, std::string>> InferenceClient::preview(const std::string& streamId,
                                                                          uint64_t after) const {
    httplib::Client cli(baseUrl_);
    cli.set_connection_timeout(1);
    cli.set_read_timeout(6);
    auto r = cli.Get("/v1/streams/" + streamId + "/preview.jpg?after=" + std::to_string(after), authHeaders(secret_));
    if (!r) throw InferenceUnavailable("inference unreachable at " + baseUrl_);
    if (r->status != 200) return std::nullopt;
    uint64_t seq = 0;
    try { seq = std::stoull(r->get_header_value("X-Frame-Seq")); } catch (...) {}
    return std::make_pair(seq, std::move(r->body));
}

std::optional<std::string> InferenceClient::frameImage(const std::string& streamId, uint64_t frame) const {
    httplib::Client cli(baseUrl_);
    cli.set_connection_timeout(1);
    cli.set_read_timeout(5);
    auto r = cli.Get("/v1/streams/" + streamId + "/frames/" + std::to_string(frame) + ".jpg", authHeaders(secret_));
    if (!r || r->status != 200) return std::nullopt;
    return std::move(r->body);
}

json InferenceClient::probe(const std::string& source) const {
    httplib::Client cli(baseUrl_);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(30);   // opening an RTSP camera can take a while
    auto r = cli.Post("/v1/probe", authHeaders(secret_), json{{"source", source}}.dump(), "application/json");
    if (!r) throw InferenceUnavailable("inference unreachable at " + baseUrl_);
    if (r->status != 200) throw InferenceError(r->status, errorDetail(r));
    return json::parse(r->body);
}

} // namespace guard
