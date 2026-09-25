#pragma once
#include "ai/types.h"
#include <string>
#include <memory>
#include <vector>
#include <map>
#include <functional>

#ifdef AI_HAS_TENSORRT
#include <NvInfer.h>
#endif

namespace ai {

// ---------------------------------------------------------------------------
// Low-level, isolated TensorRT inference layer.
//
// Responsibilities (ONLY):
//   * load + deserialize an .engine file
//   * create engine + execution context(s)
//   * allocate host (pinned) and device buffers by inspecting bindings
//   * run synchronous (MVP) or stream-based async inference
//   * translate TensorRT/CUDA failures into typed exceptions
//
// It does NOT know about plates, faces, or OCR. Model implementations above
// this layer own binding semantics (which tensor is the image, which is out,
// and the preprocessing/postprocessing around execute()).
//
// Thread-safety: one TrtEngine is intended to be used from a single worker.
// For concurrent workers either give each worker its own TrtEngine, or use
// per-call execution contexts (see makeContext()). TensorRT execution
// contexts are not shareable across threads for concurrent enqueue.
// ---------------------------------------------------------------------------

class TrtError : public std::runtime_error {
public:
    explicit TrtError(const std::string& m) : std::runtime_error(m) {}
};

// Compiled buffer binding summary; exposed for model layers to inspect dims.
struct TrtBinding {
    std::string name;
    bool isInput = false;
    std::vector<int64_t> dims;   // without batch implicit if any
    std::string dtype;           // "fp32","fp16","int8",...
    std::size_t elementSize = 0;
};

class TrtEngine {
public:
    struct Spec {
        std::string enginePath;
        int deviceId = 0;
        bool useFp16 = true;
        bool useInt8 = false;
        int contextCount = 1;      // execution contexts to create (per-worker pool)
    };

    explicit TrtEngine(Spec spec);
    ~TrtEngine();

    // Deserializes the engine, creates contexts, allocates buffers.
    void load();

    // Runs one dummy input through to prime CUDA/TensorRT lazy init.
    void warmUp();

    // Runs inference synchronously on the caller's stream (0 = default).
    // in/out vectors are host buffers (pinned or plain). The implementation
    // copies host->device, enqueues, syncs, copies device->host.
    void execute(int streamForAsync,                 // cudaStream_t or 0
                 const std::vector<Tensor>& inputs,
                 std::vector<Tensor>& outputs,
                 int contextIndex = 0);

    // Inspect the network's bindings (names, dims, dtypes) for a model layer.
    [[nodiscard]] const std::vector<TrtBinding>& bindings() const { return bindings_; }
    [[nodiscard]] std::vector<int64_t> inputShape(const std::string& name) const;
    [[nodiscard]] std::vector<int64_t> outputShape(const std::string& name) const;
    // For profile 0 of a dynamic input: returns {min, opt, max} shape vectors.
    // Empty if the tensor is not an input or the engine has no profiles.
    [[nodiscard]] std::vector<std::vector<int64_t>> profileShapes(const std::string& name) const;
    [[nodiscard]] bool isBuiltInFp16() const { return fp16_; }
    [[nodiscard]] bool isBuiltInInt8() const { return int8_; }
    [[nodiscard]] std::string path() const { return spec_.enginePath; }

private:
    void destroy();
    // Grow (or create) the device buffer for `name` so it can hold `bytes`.
    void ensureBuffer(const std::string& name, std::size_t bytes);
    bool isInputTensor(const std::string& name) const;

    Spec spec_;
#ifdef AI_HAS_TENSORRT
    // Typed TensorRT handles, owned RAII (destroyed in destroy()).
    nvinfer1::IRuntime*           runtime_ = nullptr;
    nvinfer1::ICudaEngine*        engine_  = nullptr;
    std::vector<nvinfer1::IExecutionContext*> contexts_;
    // device buffer per tensor name (cudaMalloc'd), reused across calls
    std::map<std::string, void*>  deviceBuffers_;
    // capacity (bytes) currently allocated per tensor buffer
    std::map<std::string, std::size_t> deviceBufferBytes_;
#endif
    std::vector<TrtBinding> bindings_;
    bool fp16_ = false;
    bool int8_ = false;
    bool loaded_ = false;
};

} // namespace ai
