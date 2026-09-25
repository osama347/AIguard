#include "ai/trt_engine.h"
#include "ai/logging.h"

#ifdef AI_HAS_TENSORRT

#include <NvInfer.h>
#include <cuda_runtime_api.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>

using namespace nvinfer1;

namespace ai {

// ---------------------------------------------------------------------------
// This implementation targets the modern, explicit-batch TensorRT API
// (TensorRT 8.6+ / 10.x) which uses the *tensor-name* API:
//   getNbIOTensors / getIOTensorName / getTensorShape / setTensorAddress
//   + enqueueV3 on the execution context.
// The older implicit-batch binding-index API (getBindingIndex/
// hasImplicitBatchDimension/getBindingDimensions) is removed in these versions.
// ---------------------------------------------------------------------------

namespace {
class TrtLogger : public ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override {
        if (severity == Severity::kERROR || severity == Severity::kINTERNAL_ERROR) {
            LOG_ERROR << "[trt] " << (msg ? msg : "");
        }
        // kWARNING/info are intentionally suppressed by default
    }
};
TrtLogger gTrtLogger;

std::string dtypeName(DataType t) {
    switch (t) {
        case DataType::kFLOAT: return "fp32";
        case DataType::kHALF:  return "fp16";
        case DataType::kINT8:  return "int8";
        case DataType::kINT32: return "int32";
        case DataType::kBOOL:  return "bool";
        case DataType::kUINT8: return "uint8";
        default: return "unknown";
    }
}

std::size_t elementSize(DataType t) {
    switch (t) {
        case DataType::kFLOAT: return 4;
        case DataType::kHALF:  return 2;
        case DataType::kINT8:  return 1;
        case DataType::kINT32: return 4;
        case DataType::kBOOL:  return 1;
        default: return 4;
    }
}

std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw TrtError("cannot open engine file: " + path);
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<char> buf(size);
    if (size > 0) f.read(buf.data(), size);
    if (!f && !f.eof()) throw TrtError("failed reading engine file: " + path);
    return buf;
}

int64_t volumeOf(const Dims& d) {
    int64_t vol = 1;
    for (int i = 0; i < d.nbDims; ++i) {
        if (d.d[i] > 0) vol *= d.d[i];
    }
    return vol;
}

// Copy a Dims into a std::vector<int64_t>.
std::vector<int64_t> dimsVec(const Dims& d) {
    std::vector<int64_t> out;
    out.reserve(d.nbDims);
    for (int i = 0; i < d.nbDims; ++i) out.push_back(d.d[i]);
    return out;
}

std::string fmtDims(const std::vector<int64_t>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += std::to_string(v[i]);
    }
    s += "]";
    return s;
}
} // anonymous namespace

TrtEngine::TrtEngine(Spec spec) : spec_(std::move(spec)) {}

TrtEngine::~TrtEngine() { destroy(); }

void TrtEngine::destroy() {
    for (auto& [k, p] : deviceBuffers_) {
        if (p) { cudaFree(p); }
        p = nullptr;
    }
    deviceBuffers_.clear();
    deviceBufferBytes_.clear();
    for (auto* c : contexts_) { delete c; }
    contexts_.clear();
    delete engine_;
    delete runtime_;
    engine_ = nullptr; runtime_ = nullptr;
    loaded_ = false;
}

void TrtEngine::ensureBuffer(const std::string& name, std::size_t bytes) {
    auto it = deviceBufferBytes_.find(name);
    if (it != deviceBufferBytes_.end() && it->second >= bytes)
        return;  // existing buffer is large enough
    // Free the old buffer (if any) and allocate a fresh one.
    auto buf = deviceBuffers_.find(name);
    if (buf != deviceBuffers_.end() && buf->second) { cudaFree(buf->second); }
    void* dev = nullptr;
    if (cudaMalloc(&dev, bytes) != cudaSuccess)
        throw TrtError("cudaMalloc failed for " + name + " (" + std::to_string(bytes) + " bytes)");
    deviceBuffers_[name] = dev;
    deviceBufferBytes_[name] = bytes;
}

void TrtEngine::load() {
    destroy();

    // Select device
    int devCount = 0;
    if (cudaGetDeviceCount(&devCount) != cudaSuccess || devCount == 0)
        throw TrtError("no CUDA device present");
    int dev = std::min(spec_.deviceId, devCount - 1);
    if (cudaSetDevice(dev) != cudaSuccess)
        throw TrtError("cudaSetDevice failed for device " + std::to_string(dev));

    auto blob = readFile(spec_.enginePath);

    runtime_ = createInferRuntime(gTrtLogger);
    if (!runtime_) throw TrtError("failed to create TensorRT runtime");

    engine_ = runtime_->deserializeCudaEngine(blob.data(), blob.size());
    if (!engine_) throw TrtError("engine deserialization failed: " + spec_.enginePath);

    // Introspect IO tensors (modern tensor-name API).
    const int nTensors = engine_->getNbIOTensors();
    bindings_.clear();
    for (int i = 0; i < nTensors; ++i) {
        const char* name = engine_->getIOTensorName(i);
        if (!name) continue;
        TrtBinding b;
        b.name = name;
        b.isInput = (engine_->getTensorIOMode(name) == TensorIOMode::kINPUT);
        b.dims = dimsVec(engine_->getTensorShape(name));
        DataType dt = engine_->getTensorDataType(name);
        b.dtype = dtypeName(dt);
        b.elementSize = elementSize(dt);
        bindings_.push_back(b);

        // Detect engine precision from the first input's dtype.
        if (b.isInput) {
            if (dt == DataType::kHALF) fp16_ = true;
            if (dt == DataType::kINT8) int8_ = true;
        }
        LOG_INFO << "tensor[" << i << "] name=" << b.name
                 << " input=" << b.isInput << " dtype=" << b.dtype
                 << " dims=" << fmtDims(b.dims);
        // For a dynamic input, log the profile 0 min/opt/max shapes.
        if (b.isInput) {
            bool dyn = false;
            for (int64_t d : b.dims) if (d < 0) { dyn = true; break; }
            if (dyn) {
                auto ps = profileShapes(b.name);
                if (ps.size() == 3) {
                    LOG_INFO << "tensor[" << i << "] profile(min/opt/max) "
                             << fmtDims(ps[0]) << " / " << fmtDims(ps[1])
                             << " / " << fmtDims(ps[2]);
                }
            }
        }
    }

    // Create execution contexts.
    if (spec_.contextCount <= 0) spec_.contextCount = 1;
    for (int c = 0; c < spec_.contextCount; ++c) {
        IExecutionContext* ctx = engine_->createExecutionContext();
        if (!ctx) throw TrtError("failed to create execution context");
        contexts_.push_back(ctx);
    }

    // Allocate an initial device buffer per tensor. For dynamic-shape tensors
    // (any dim == -1) the declared volume is not usable, so we allocate a small
    // default and grow it lazily in execute() to the actual runtime volume.
    for (const auto& b : bindings_) {
        int64_t vol = 1;
        for (int64_t d : b.dims) vol *= (d > 0 ? d : 1);
        std::size_t bytes = static_cast<std::size_t>(vol > 0 ? vol : 1) * b.elementSize;
        if (bytes < 4) bytes = 4;
        void* dev = nullptr;
        if (cudaMalloc(&dev, bytes) != cudaSuccess)
            throw TrtError("cudaMalloc failed for tensor " + b.name +
                           " (" + std::to_string(bytes) + " bytes)");
        deviceBuffers_[b.name] = dev;
        deviceBufferBytes_[b.name] = bytes;
    }

    // Bind every tensor to its device buffer address on every context.
    for (IExecutionContext* ctx : contexts_) {
        for (const auto& b : bindings_) {
            ctx->setTensorAddress(b.name.c_str(), deviceBuffers_[b.name]);
        }
    }

    loaded_ = true;
    LOG_INFO << "Loaded TensorRT engine: " << spec_.enginePath
             << (fp16_ ? " (fp16)" : "") << (int8_ ? " (int8)" : "");
}

void TrtEngine::warmUp() {
    if (!loaded_) load();
    std::vector<Tensor> ins;
    bool anyDynamic = false;
    for (const auto& b : bindings_) {
        if (!b.isInput) continue;
        // Skip dynamic-shape inputs (any dim == -1): we can't fabricate a
        // valid shape here. Such engines are warmed up by the first real
        // inference (the model layer supplies the concrete shape).
        bool dynamic = false;
        for (int64_t d : b.dims) if (d < 0) { dynamic = true; break; }
        if (dynamic) { anyDynamic = true; continue; }

        int64_t vol = 1;
        for (int64_t v : b.dims) vol *= (v > 0 ? v : 1);
        Tensor t;
        t.name = b.name;
        t.shape = b.dims;
        t.data.assign(static_cast<std::size_t>(vol), 0.0f);
        ins.push_back(std::move(t));
    }
    if (ins.empty() && !anyDynamic) {
        LOG_WARN << "Engine has no non-dynamic inputs to warm up: " << spec_.enginePath;
        return;
    }
    if (!ins.empty()) {
        std::vector<Tensor> outs;
        execute(0, ins, outs, 0);
    }
    LOG_INFO << "Warmed up engine: " << spec_.enginePath;
}

void TrtEngine::execute(int streamForAsync,
                        const std::vector<Tensor>& inputs,
                        std::vector<Tensor>& outputs,
                        int contextIndex) {
    if (!loaded_) load();
    IExecutionContext* ctx = contexts_.at(static_cast<std::size_t>(contextIndex));
    cudaStream_t stream = streamForAsync ? reinterpret_cast<cudaStream_t>(streamForAsync) : nullptr;

    // Does the engine have any dynamic (un-specified) input dimension? If so
    // we must call setInputShape before enqueue with the concrete shapes (the
    // model layer supplies these in Tensor.shape).
    bool dynamic = false;
    for (const auto& b : bindings_)
        if (b.isInput)
            for (int64_t d : b.dims)
                if (d < 0) { dynamic = true; break; }

    // 1. Size + copy each input to its device buffer, and (if dynamic) set shape.
    for (const auto& in : inputs) {
        auto it = deviceBuffers_.find(in.name);
        if (it == deviceBuffers_.end())
            throw TrtError("unknown input tensor: " + in.name);
        // Use raw bytes if present (for uint8/int8 tensors), else float data.
        const void* src = in.raw.empty() ? static_cast<const void*>(in.data.data())
                                         : static_cast<const void*>(in.raw.data());
        std::size_t bytes = in.raw.empty() ? in.data.size() * sizeof(float)
                                           : in.raw.size() * sizeof(uint8_t);
        ensureBuffer(in.name, std::max<std::size_t>(bytes, 4));
        ctx->setTensorAddress(in.name.c_str(), deviceBuffers_[in.name]);  // rebind if realloc'd
        if (cudaMemcpyAsync(deviceBuffers_[in.name], src, bytes,
                            cudaMemcpyHostToDevice, stream) != cudaSuccess)
            throw TrtError("cudaMemcpy H2D failed for " + in.name);

        if (dynamic && !in.shape.empty()) {
            Dims d; d.nbDims = (int)in.shape.size();
            for (std::size_t k = 0; k < in.shape.size(); ++k) d.d[k] = (int)in.shape[k];
            if (!ctx->setInputShape(in.name.c_str(), d))
                throw TrtError("setInputShape failed for " + in.name);
        }
    }

    // 2. For every output, resolve its (possibly dynamic) runtime shape now
    //    that input shapes are set, then size/rebind its device buffer.
    for (const auto& b : bindings_) {
        if (b.isInput) continue;
        Dims outDims = ctx->getTensorShape(b.name.c_str());
        int64_t vol = volumeOf(outDims);
        if (vol <= 0) vol = 1;
        std::size_t bytes = static_cast<std::size_t>(vol) * std::max<std::size_t>(b.elementSize, 4);
        ensureBuffer(b.name, std::max<std::size_t>(bytes, 4));
        ctx->setTensorAddress(b.name.c_str(), deviceBuffers_[b.name]);
    }

    // 3. Enqueue.
    if (!ctx->enqueueV3(stream))
        throw TrtError("TensorRT enqueueV3 failed");

    if (stream) cudaStreamSynchronize(stream);
    else        cudaDeviceSynchronize();

    // 4. Copy each output tensor back to host (D2H).
    outputs.clear();
    for (const auto& b : bindings_) {
        if (b.isInput) continue;
        Dims d = ctx->getTensorShape(b.name.c_str());
        int64_t vol = volumeOf(d);
        if (vol <= 0) vol = 1;

        Tensor out;
        out.name = b.name;
        out.shape = dimsVec(d);
        out.data.assign(static_cast<std::size_t>(vol), 0.0f);

        auto it = deviceBuffers_.find(b.name);
        if (it == deviceBuffers_.end())
            throw TrtError("unknown output tensor: " + b.name);
        std::size_t bytes = static_cast<std::size_t>(vol) * sizeof(float);
        if (cudaMemcpyAsync(out.data.data(), it->second, bytes,
                            cudaMemcpyDeviceToHost, stream) != cudaSuccess)
            throw TrtError("cudaMemcpy D2H failed for " + b.name);
        if (stream) cudaStreamSynchronize(stream);
        outputs.push_back(std::move(out));
    }
}

std::vector<int64_t> TrtEngine::inputShape(const std::string& name) const {
    for (const auto& b : bindings_)
        if (b.isInput && b.name == name) return b.dims;
    return {};
}
std::vector<int64_t> TrtEngine::outputShape(const std::string& name) const {
    for (const auto& b : bindings_)
        if (!b.isInput && b.name == name) return b.dims;
    return {};
}

std::vector<std::vector<int64_t>> TrtEngine::profileShapes(const std::string& name) const {
    std::vector<std::vector<int64_t>> out;
#ifndef AI_HAS_TENSORRT
    (void)name; (void)out;
    return out;
#else
    if (!engine_ || !isInputTensor(name)) return out;
    int nb = engine_->getNbOptimizationProfiles();
    if (nb <= 0) return out;
    const char* nm = name.c_str();
    // For each profile dimension of the first dim tensor (already 0 for our use).
    for (int mode : {0, 1, 2}) {   // 0=min 1=opt 2=max
        Dims d;
        switch (mode) {
            case 0: d = engine_->getProfileShape(nm, 0, nvinfer1::OptProfileSelector::kMIN); break;
            case 1: d = engine_->getProfileShape(nm, 0, nvinfer1::OptProfileSelector::kOPT); break;
            default: d = engine_->getProfileShape(nm, 0, nvinfer1::OptProfileSelector::kMAX); break;
        }
        out.push_back(dimsVec(d));
    }
    return out;
#endif
}

bool TrtEngine::isInputTensor(const std::string& name) const {
    for (const auto& b : bindings_)
        if (b.isInput && b.name == name) return true;
    return false;
}


#else // !AI_HAS_TENSORRT
namespace ai {
TrtEngine::TrtEngine(Spec spec) : spec_(std::move(spec)) {}
TrtEngine::~TrtEngine() = default;
void TrtEngine::load() { loaded_ = false; }
void TrtEngine::warmUp() {}
void TrtEngine::execute(int, const std::vector<Tensor>&, std::vector<Tensor>&, int) {}
std::vector<int64_t> TrtEngine::inputShape(const std::string&) const { return {}; }
std::vector<int64_t> TrtEngine::outputShape(const std::string&) const { return {}; }
}
#endif

} // namespace ai
