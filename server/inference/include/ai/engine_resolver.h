#pragma once
#include "ai/config.h"
#include <functional>
#include <string>

namespace ai {

// ---------------------------------------------------------------------------
// EngineResolver: turns a model's ONNX source into a TensorRT engine that is
// valid on *this* device.
//
// A serialized engine only works with the GPU and TensorRT build that created
// it, so every engine carries a sidecar "<key>.engine.json" recording
// {trt_version, device, onnx_hash, precision}. Resolution order:
//   1. explicit `engine` path in the model config (used as-is)
//   2. <engine_cache_dir>/<key>.engine   with a matching sidecar
//   3. <prebuilt_dir>/<key>.engine       with a matching sidecar (copied to cache)
//   4. build from the ONNX file into the cache (slow: minutes on first boot)
// ---------------------------------------------------------------------------
class EngineResolver {
public:
    using Progress = std::function<void(const std::string& message)>;

    explicit EngineResolver(const TensorRTConfig& cfg);

    // Returns a path to a usable engine for `key`. Throws std::runtime_error.
    std::string resolve(const std::string& key, const ModelConfig& mc, const Progress& progress);

    // Identity of this runtime (TensorRT version + GPU + board), for diagnostics.
    const std::string& trtVersion() const { return trtVersion_; }
    const std::string& device() const { return device_; }

private:
    bool sidecarMatches(const std::string& enginePath, const std::string& onnxHash) const;
    void writeSidecar(const std::string& enginePath, const std::string& onnxHash) const;
    void build(const std::string& onnxPath, const std::string& enginePath) const;

    TensorRTConfig cfg_;
    std::string trtVersion_;
    std::string device_;
};

// Content fingerprint of a file (FNV-1a 64, hex). Exposed for tests/tools.
std::string fileFingerprint(const std::string& path);

} // namespace ai
