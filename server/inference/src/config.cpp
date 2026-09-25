#include "ai/config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace ai {

using json = nlohmann::json;

namespace {

// Small helpers to read optionally-present fields with defaults.
int readInt(const json& j, const std::string& key, int def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<int>();
    return def;
}
std::size_t readSizeT(const json& j, const std::string& key, std::size_t def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<std::size_t>();
    return def;
}
float readFloat(const json& j, const std::string& key, float def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<float>();
    return def;
}
std::string readStr(const json& j, const std::string& key, const std::string& def) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return def;
}
bool readBool(const json& j, const std::string& key, bool def) {
    if (j.contains(key) && j[key].is_boolean()) return j[key].get<bool>();
    return def;
}

std::vector<std::string> readStrArray(const json& j, const std::string& key) {
    std::vector<std::string> out;
    if (j.contains(key) && j[key].is_array()) {
        for (const auto& v : j[key]) out.push_back(v.get<std::string>());
    }
    return out;
}

ModelConfig parseModel(const json& m) {
    ModelConfig c;
    c.onnxPath      = readStr(m, "onnx", "");
    c.enginePath    = readStr(m, "engine", "");
    c.inputWidth    = readInt(m, "input_w", 0);
    c.inputHeight   = readInt(m, "input_h", 0);
    c.confThreshold = readFloat(m, "conf", 0.5f);
    c.nmsThreshold  = readFloat(m, "nms", 0.45f);
    c.labels        = readStrArray(m, "labels");
    c.alphabet      = readStr(m, "alphabet", "");
    c.embeddingDim  = readInt(m, "embedding_dim", 0);
    c.colorOrder    = readStr(m, "channels", "rgb") == "bgr" ? 0 : 1;
    c.normalize     = readFloat(m, "normalize", 255.f);
    c.mean          = readFloat(m, "mean", 0.f);
    if (m.contains("input") && m["input"].is_object()) {
        c.inputWidth  = readInt(m["input"], "w", c.inputWidth);
        c.inputHeight = readInt(m["input"], "h", c.inputHeight);
    }
    return c;
}

// Resolve a possibly-relative path against the config file directory.
std::string resolvePath(const std::filesystem::path& base, const std::string& p) {
    if (p.empty() || std::filesystem::path(p).is_absolute()) return p;
    return (base / p).lexically_normal().string();
}

} // namespace

bool loadConfig(const std::string& path, AppConfig& cfg, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open config file: " + path; return false; }

    json doc;
    try {
        doc = json::parse(f);
    } catch (const json::parse_error& e) {
        err = "config JSON parse error: " + std::string(e.what());
        return false;
    }
    const std::filesystem::path base = std::filesystem::path(path).parent_path();

    if (doc.contains("server") && doc["server"].is_object()) {
        const auto& s = doc["server"];
        cfg.server.host            = readStr(s, "host", cfg.server.host);
        cfg.server.port            = readInt(s, "port", cfg.server.port);
        cfg.server.threads         = readInt(s, "threads", cfg.server.threads);
        cfg.server.maxRequestBytes = readSizeT(s, "max_request_bytes", cfg.server.maxRequestBytes);
        cfg.server.timeoutS        = readInt(s, "timeout_s", cfg.server.timeoutS);
        cfg.server.secret          = readStr(s, "secret", cfg.server.secret);
    }

    if (doc.contains("tensorrt") && doc["tensorrt"].is_object()) {
        const auto& t = doc["tensorrt"];
        cfg.tensorrt.deviceId       = readInt(t, "device_id", 0);
        cfg.tensorrt.precision      = readStr(t, "precision", "fp16");
        cfg.tensorrt.warmup         = readBool(t, "warmup", true);
        cfg.tensorrt.workspaceMb    = readSizeT(t, "workspace_mb", cfg.tensorrt.workspaceMb);
        cfg.tensorrt.engineCacheDir = resolvePath(base, readStr(t, "engine_cache_dir", ""));
        cfg.tensorrt.prebuiltDir    = resolvePath(base, readStr(t, "prebuilt_dir", ""));
    }

    if (doc.contains("models") && doc["models"].is_object()) {
        for (auto it = doc["models"].begin(); it != doc["models"].end(); ++it) {
            ModelConfig mc = parseModel(it.value());
            mc.onnxPath   = resolvePath(base, mc.onnxPath);
            mc.enginePath = resolvePath(base, mc.enginePath);
            cfg.models[it.key()] = mc;
        }
    }

    if (doc.contains("video") && doc["video"].is_object()) {
        const auto& v = doc["video"];
        if (v.contains("sample_fps") && v["sample_fps"].is_number())
            cfg.video.sampleFps = v["sample_fps"].get<double>();
        cfg.video.retainFinishedS = readInt(v, "retain_finished_s", cfg.video.retainFinishedS);
    }

    if (doc.contains("live") && doc["live"].is_object()) {
        const auto& l = doc["live"];
        cfg.live.maxStreams   = readInt(l, "max_streams", cfg.live.maxStreams);
        if (l.contains("preview_fps") && l["preview_fps"].is_number())
            cfg.live.previewFps = l["preview_fps"].get<double>();
        cfg.live.previewWidth = readInt(l, "preview_width", cfg.live.previewWidth);
        cfg.live.jpegQuality  = readInt(l, "jpeg_quality", cfg.live.jpegQuality);
        cfg.live.eventBuffer  = readInt(l, "event_buffer", cfg.live.eventBuffer);
        cfg.live.frameImages  = readInt(l, "frame_images", cfg.live.frameImages);
    }

    if (doc.contains("logging") && doc["logging"].is_object()) {
        const auto& lg = doc["logging"];
        cfg.logging.level    = readStr(lg, "level", "info");
        cfg.logging.jsonLogs = readBool(lg, "json_logs", false);
    }

    if (const char* s = std::getenv("GUARD_INFERENCE_SECRET")) cfg.server.secret = s;
    if (const char* c = std::getenv("GUARD_ENGINE_CACHE")) cfg.tensorrt.engineCacheDir = c;
    return true;
}

} // namespace ai
