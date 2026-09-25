#include "ai/engine_resolver.h"
#include "ai/logging.h"
#include "ai/timing.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

#ifdef AI_HAS_TENSORRT
#include <NvInfer.h>
#include <NvOnnxParser.h>
#include <cuda_runtime_api.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ai {

namespace {

std::string readTrimmed(const std::string& path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    while (!s.empty() && (s.back() == '\0' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

#ifdef AI_HAS_TENSORRT
class BuildLogger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kERROR) LOG_ERROR << "[trt-build] " << msg;
        else if (severity == Severity::kWARNING) LOG_DEBUG << "[trt-build] " << msg;
    }
};
#endif

} // namespace

std::string fileFingerprint(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    uint64_t h = 1469598103934665603ULL;
    std::vector<char> buf(1 << 20);
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        for (std::streamsize i = 0; i < f.gcount(); ++i) {
            h ^= static_cast<unsigned char>(buf[i]);
            h *= 1099511628211ULL;
        }
    }
    char out[17];
    std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(h));
    return out;
}

EngineResolver::EngineResolver(const TensorRTConfig& cfg) : cfg_(cfg) {
#ifdef AI_HAS_TENSORRT
    int v = getInferLibVersion();   // e.g. 100300 for 10.3.0
    trtVersion_ = std::to_string(v / 10000) + "." + std::to_string((v / 100) % 100) + "." +
                  std::to_string(v % 100);
    cudaDeviceProp prop{};
    if (cudaGetDeviceProperties(&prop, cfg.deviceId) == cudaSuccess) {
        device_ = std::string(prop.name) + " sm_" + std::to_string(prop.major) +
                  std::to_string(prop.minor);
    }
#endif
    std::string board = readTrimmed("/proc/device-tree/model");
    if (!board.empty()) device_ += " | " + board;
}

bool EngineResolver::sidecarMatches(const std::string& enginePath, const std::string& onnxHash) const {
    std::ifstream f(enginePath + ".json");
    if (!f || !fs::exists(enginePath)) return false;
    try {
        json j = json::parse(f);
        return j.value("trt_version", "") == trtVersion_ && j.value("device", "") == device_ &&
               j.value("onnx_hash", "") == onnxHash && j.value("precision", "") == cfg_.precision;
    } catch (...) {
        return false;
    }
}

void EngineResolver::writeSidecar(const std::string& enginePath, const std::string& onnxHash) const {
    json j{{"trt_version", trtVersion_}, {"device", device_},
           {"onnx_hash", onnxHash}, {"precision", cfg_.precision}};
    std::ofstream(enginePath + ".json") << j.dump(2) << "\n";
}

std::string EngineResolver::resolve(const std::string& key, const ModelConfig& mc,
                                    const Progress& progress) {
    if (!mc.enginePath.empty()) {
        if (!fs::exists(mc.enginePath)) throw std::runtime_error("engine not found: " + mc.enginePath);
        return mc.enginePath;
    }
    if (mc.onnxPath.empty() || !fs::exists(mc.onnxPath))
        throw std::runtime_error("model '" + key + "' has no ONNX source: " + mc.onnxPath);
    if (cfg_.engineCacheDir.empty())
        throw std::runtime_error("tensorrt.engine_cache_dir is not configured");

    const std::string hash = fileFingerprint(mc.onnxPath);
    fs::create_directories(cfg_.engineCacheDir);
    const std::string cached = (fs::path(cfg_.engineCacheDir) / (key + ".engine")).string();

    if (sidecarMatches(cached, hash)) return cached;

    if (!cfg_.prebuiltDir.empty()) {
        const std::string pre = (fs::path(cfg_.prebuiltDir) / (key + ".engine")).string();
        if (sidecarMatches(pre, hash)) {
            fs::copy_file(pre, cached, fs::copy_options::overwrite_existing);
            fs::copy_file(pre + ".json", cached + ".json", fs::copy_options::overwrite_existing);
            LOG_INFO << "Using prebuilt engine for " << key;
            return cached;
        }
    }

    progress("building TensorRT engine for " + key + " (first run, this can take minutes)");
    LOG_INFO << "Building engine for " << key << " from " << mc.onnxPath
             << " [TensorRT " << trtVersion_ << ", " << device_ << "]";
    Stopwatch sw;
    const std::string tmp = cached + ".tmp";
    build(mc.onnxPath, tmp);
    fs::rename(tmp, cached);
    writeSidecar(cached, hash);
    LOG_INFO << "Built " << key << " in " << static_cast<int>(sw.elapsedMs() / 1000) << " s";
    return cached;
}

void EngineResolver::build(const std::string& onnxPath, const std::string& enginePath) const {
#ifdef AI_HAS_TENSORRT
    using namespace nvinfer1;
    BuildLogger logger;
    std::unique_ptr<IBuilder> builder(createInferBuilder(logger));
    if (!builder) throw std::runtime_error("createInferBuilder failed");
    std::unique_ptr<INetworkDefinition> network(builder->createNetworkV2(0));
    std::unique_ptr<nvonnxparser::IParser> parser(nvonnxparser::createParser(*network, logger));
    if (!parser->parseFromFile(onnxPath.c_str(), static_cast<int>(ILogger::Severity::kWARNING))) {
        std::string msg = "failed to parse " + onnxPath;
        for (int i = 0; i < parser->getNbErrors(); ++i) msg += "; " + std::string(parser->getError(i)->desc());
        throw std::runtime_error(msg);
    }

    std::unique_ptr<IBuilderConfig> config(builder->createBuilderConfig());
    config->setMemoryPoolLimit(MemoryPoolType::kWORKSPACE, cfg_.workspaceMb << 20);
    if (cfg_.precision == "fp16") config->setFlag(BuilderFlag::kFP16);

    // Dynamic dimensions (e.g. a free batch axis) are pinned to 1: the service
    // runs one crop / frame per inference.
    IOptimizationProfile* profile = nullptr;
    for (int i = 0; i < network->getNbInputs(); ++i) {
        ITensor* in = network->getInput(i);
        Dims d = in->getDimensions();
        bool dynamic = false;
        for (int k = 0; k < d.nbDims; ++k) {
            if (d.d[k] < 0) { d.d[k] = 1; dynamic = true; }
        }
        if (!dynamic) continue;
        if (!profile) profile = builder->createOptimizationProfile();
        for (auto sel : {OptProfileSelector::kMIN, OptProfileSelector::kOPT, OptProfileSelector::kMAX})
            profile->setDimensions(in->getName(), sel, d);
    }
    if (profile) config->addOptimizationProfile(profile);

    std::unique_ptr<IHostMemory> plan(builder->buildSerializedNetwork(*network, *config));
    if (!plan) throw std::runtime_error("TensorRT engine build failed for " + onnxPath);
    std::ofstream out(enginePath, std::ios::binary);
    out.write(static_cast<const char*>(plan->data()), static_cast<std::streamsize>(plan->size()));
    if (!out) throw std::runtime_error("cannot write " + enginePath);
#else
    (void)onnxPath; (void)enginePath;
    throw std::runtime_error("this build has no TensorRT support");
#endif
}

} // namespace ai
