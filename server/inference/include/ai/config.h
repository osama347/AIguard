#pragma once
#include <string>
#include <vector>
#include <map>

namespace ai {

// ---------------------------------------------------------------------------
// Configuration mirroring config/inference.json. Relative paths are resolved
// against the directory of the config file.
// ---------------------------------------------------------------------------

struct ModelConfig {
    std::string onnxPath;        // source model; engines are built from this
    std::string enginePath;      // optional explicit engine (skips resolution)
    int inputWidth = 0;
    int inputHeight = 0;
    float confThreshold = 0.5f;
    float nmsThreshold = 0.45f;
    std::vector<std::string> labels;
    std::string alphabet;        // OCR label set
    int embeddingDim = 0;        // face embedding model
    int colorOrder = 1;          // 1 = BGR->RGB (OpenCV->torch default), 0 = feed as-is (BGR)
    float normalize = 255.f;     // divisor for pixel scaling: 255 -> /255, 1 -> raw 0-255
    float mean = 0.f;            // subtract bias: value = pixel/normalize - mean
};

struct TensorRTConfig {
    int deviceId = 0;
    std::string precision = "fp16";   // fp32 | fp16
    bool warmup = true;
    std::size_t workspaceMb = 1024;   // builder workspace when building from ONNX
    std::string engineCacheDir;       // writable; engines built here
    std::string prebuiltDir;          // read-only engines shipped with the package
};

struct ServerConfig {
    std::string host = "127.0.0.1";   // inference is internal: loopback only
    int port = 8081;
    int threads = 4;
    std::size_t maxRequestBytes = 64ull * 1024 * 1024;
    int timeoutS = 60;
    std::string secret;               // shared secret expected in X-Guard-Secret
};

struct VideoConfig {
    double sampleFps = 5.0;           // frames analysed per second of video
    int retainFinishedS = 600;        // keep finished job streams for replay
};

struct LiveConfig {
    int maxStreams = 8;               // concurrent live camera streams
    double previewFps = 10.0;         // JPEG preview frames per second
    int previewWidth = 960;           // preview is downscaled to this width
    int jpegQuality = 75;
    int eventBuffer = 600;            // frame events kept per stream for resume
    int frameImages = 60;             // analysed frames kept as JPEG (snapshots)
};

struct LogConfig {
    std::string level = "info";       // debug|info|warn|error
    bool jsonLogs = false;
};

struct AppConfig {
    ServerConfig    server;
    TensorRTConfig  tensorrt;
    std::map<std::string, ModelConfig> models;   // model key -> config
    VideoConfig     video;
    LiveConfig      live;
    LogConfig       logging;
};

// Load config from a JSON file. Returns false and sets err on failure.
// Environment overrides: GUARD_INFERENCE_SECRET, GUARD_ENGINE_CACHE.
bool loadConfig(const std::string& path, AppConfig& cfg, std::string& err);

} // namespace ai
