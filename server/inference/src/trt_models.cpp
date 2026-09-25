#include "ai/trt_models.h"
#include "ai/logging.h"
#include <algorithm>
#include <cmath>

#ifdef AI_HAS_TENSORRT
#include <opencv2/imgproc.hpp>
namespace {
constexpr float kVariance[] = {0.0039215686f, 0.f, 0.f, 1.f}; // /255 with zero-mean scale
}
#endif

namespace ai {

// ---------------------------------------------------------------------------
// YOLO detector: letterbox + normalize + NCHW, then decode.
// ---------------------------------------------------------------------------

TrtYoloDetector::TrtYoloDetector(std::shared_ptr<TrtEngine> engine, ModelConfig cfg,
                                 std::string displayName)
    : engine_(std::move(engine)), cfg_(std::move(cfg)), displayName_(std::move(displayName)) {
    if (cfg_.labels.empty()) cfg_.labels = {"object"};
}

Tensor TrtYoloDetector::preprocess(const cv::Mat& frame) {
#ifdef AI_HAS_TENSORRT
    int modelW = cfg_.inputWidth, modelH = cfg_.inputHeight;
    const int srcW = frame.cols, srcH = frame.rows;
    const float ratio = std::min((float)modelW / srcW, (float)modelH / srcH);
    const int newW = (int)std::round(srcW * ratio);
    const int newH = (int)std::round(srcH * ratio);

    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(newW, newH), 0, 0, cv::INTER_LINEAR);

    // letterbox to model size with zero padding
    cv::Mat canvas(modelH, modelW, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(0, 0, newW, newH)));

    // BGR -> RGB -> float -> NCHW with /255
    Tensor t;
    t.name = engine_->bindings().empty() ? "input" : engine_->bindings()[0].name;
    t.shape = {1, 3, modelH, modelW};
    t.data.assign(static_cast<size_t>(1) * 3 * modelH * modelW, 0.f);

    const float* meanUnused = nullptr; (void)meanUnused;
    // image is CV_8UC3 contiguous
    const int channels = 3;
    const float scale = 1.f / cfg_.normalize;
    size_t plane = static_cast<size_t>(modelH) * modelW;
    for (int y = 0; y < modelH; ++y) {
        const uchar* row = canvas.ptr<uchar>(y);
        for (int x = 0; x < modelW; ++x) {
            size_t pixel = static_cast<size_t>(y) * modelW + x;
            const uchar* px = row + x * channels;
            if (cfg_.colorOrder == 1) { // BGR->RGB
                t.data[0 * plane + pixel] = px[2] * scale;
                t.data[1 * plane + pixel] = px[1] * scale;
                t.data[2 * plane + pixel] = px[0] * scale;
            } else {                     // feed OpenCV BGR as-is
                t.data[0 * plane + pixel] = px[0] * scale;
                t.data[1 * plane + pixel] = px[1] * scale;
                t.data[2 * plane + pixel] = px[2] * scale;
            }
        }
    }
    outW_ = newW; outH_ = newH;
    return t;
#else
    (void)frame;
    throw TrtError("TensorRT not available in this build");
#endif
}

std::vector<Detection> TrtYoloDetector::detect(const Image& image) {
#ifdef AI_HAS_TENSORRT
    auto input = preprocess(image.mat);

    std::vector<Tensor> ins{input};
    std::vector<Tensor> outs;
    engine_->execute(0, ins, outs, 0);
    if (outs.empty()) return {};

    return decodeOutputs(outs[0], image.mat.cols, image.mat.rows,
                         cfg_.confThreshold, cfg_.nmsThreshold);
#else
    (void)image;
    return {};
#endif
}

std::vector<Detection> TrtYoloDetector::decodeOutputs(const Tensor& out,
                                                      int origW, int origH,
                                                      float confThr, float nmsThr) {
#ifdef AI_HAS_TENSORRT
    // Support two common YOLO output layouts:
    //   [1, num, 5+classes]  (xywh + objectness + classes)  OR
    //   [1, 4+num, ...]  handled generically below.
    const std::vector<float>& d = out.data;
    if (out.shape.empty()) return {};

    int64_t batches = out.shape.size() >= 1 ? out.shape[0] : 1;
    int64_t majors  = out.shape.size() >= 2 ? out.shape[1] : 0;
    int64_t cols    = out.shape.size() >= 3 ? out.shape[2] : 0;

    std::vector<Detection> dets;
    int64_t numClasses = static_cast<int64_t>(cfg_.labels.size());

    // Layout T: transposed [1, C, N] (C small = feature channels, N = anchors).
    // Memory is CHANNEL-major: channel c of anchor i lives at d[c*N + i], each
    // channel (cx,cy,w,h,conf) spanning all N anchors contiguously.
    if (majors <= 6 && cols > majors) {
        int64_t C = majors;
        int64_t N = cols;
        const float* cx  = d.data() + 0 * N;
        const float* cy  = d.data() + 1 * N;
        const float* wch = d.data() + 2 * N;
        const float* hch = d.data() + 3 * N;
        const float* cof = d.data() + 4 * N;
        for (int64_t b = 0; b < batches; ++b) {
            const float* pcx  = cx  + b * C * N;
            const float* pcy  = cy  + b * C * N;
            const float* pw   = wch + b * C * N;
            const float* ph   = hch + b * C * N;
            const float* pcof = cof + b * C * N;
            for (int64_t i = 0; i < N; ++i) {
                float conf = pcof[i];
                if (conf <= 0.0001f || conf < confThr) continue;
                float w = pw[i], h = ph[i];
                if (w <= 0 || h <= 0) continue;
                float cxv = pcx[i], cyv = pcy[i];
                const float ratio = std::min((float)cfg_.inputWidth / origW,
                                             (float)cfg_.inputHeight / origH);
                float x = (cxv - w / 2.f) / ratio;
                float y = (cyv - h / 2.f) / ratio;
                Detection det;
                det.bbox = BBox{ (int)std::max(0.f, x), (int)std::max(0.f, y),
                                 (int)(w / ratio), (int)(h / ratio) };
                det.confidence = conf;
                det.label = cfg_.labels.empty() ? "obj" : cfg_.labels[0];
                dets.push_back(det);
            }
        }
    }
    // Layout A: [1, num, 5+classes]
    else if (cols >= 5 + numClasses && majors > 0) {
        for (int64_t b = 0; b < batches; ++b) {
            for (int64_t i = 0; i < majors; ++i) {
                int64_t base = b * majors * cols + i * cols;
                float obj = d[base + 4];
                // find best class
                int bestC = 0; float bestS = 0.f;
                for (int64_t c = 0; c < numClasses; ++c) {
                    float s = d[base + 5 + c];
                    if (s > bestS) { bestS = s; bestC = (int)c; }
                }
                if (obj <= 0.0001f) continue; // skip degenerate rows
                float confidence = obj * bestS;
                // Some YOLO outputs have obj==1 placeholder and raw class scores;
                // if bestS is the meaningful score, still threshold on it.
                if (bestS < confThr && obj * bestS < confThr) continue;
                if (confidence < confThr) confidence = std::max(confidence, bestS);
                if (confidence < confThr) continue;

float cx = d[base + 0], cy = d[base + 1], w = d[base + 2], h = d[base + 3];
                if (w <= 0 || h <= 0) continue;
                // invert letterbox scale (top-left anchored, no pad shift)
                const float ratio = std::min((float)cfg_.inputWidth / origW,
                                             (float)cfg_.inputHeight / origH);
                float x = (cx - w / 2.f) / ratio;
                float y = (cy - h / 2.f) / ratio;
                float bw = w / ratio, bh = h / ratio;
                Detection det;
                det.bbox = BBox{ (int)std::max(0.f, x), (int)std::max(0.f, y),
                                 (int)bw, (int)bh };
                det.confidence = confidence;
                det.label = bestC < (int)cfg_.labels.size() ? cfg_.labels[bestC] : "obj";
                dets.push_back(det);
            }
        }
    } else {
        LOG_WARN << "Unsupported YOLO output shape for decode";
        return {};
    }

    // Non-maximum suppression (IoU)
    std::sort(dets.begin(), dets.end(), [](const Detection& a, const Detection& b) {
        return a.confidence > b.confidence;
    });
    std::vector<Detection> kept;
    for (auto& a : dets) {
        bool discard = false;
        for (auto& b : kept) {
            float ix = std::max(0, std::min(a.bbox.x + a.bbox.w, b.bbox.x + b.bbox.w) -
                                      std::max(a.bbox.x, b.bbox.x));
            float iy = std::max(0, std::min(a.bbox.y + a.bbox.h, b.bbox.y + b.bbox.h) -
                                      std::max(a.bbox.y, b.bbox.y));
            float inter = ix * iy;
            float uni = a.bbox.area() + b.bbox.area() - inter;
            float iou = uni > 0 ? inter / uni : 0.f;
            if (iou > nmsThr) { discard = true; break; }
        }
        if (!discard) kept.push_back(a);
    }
    return kept;
#else
    (void)out; (void)origW; (void)origH; (void)confThr; (void)nmsThr;
    return {};
#endif
}

// ---------------------------------------------------------------------------
// OCR / text recognition.
// ---------------------------------------------------------------------------

TrtTextRecognizer::TrtTextRecognizer(std::shared_ptr<TrtEngine> engine, ModelConfig cfg)
    : engine_(std::move(engine)), cfg_(std::move(cfg)) {
    if (cfg_.alphabet.empty()) {
        // default alphanumeric (common for plates)
        cfg_.alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    }
}

PlateResult TrtTextRecognizer::recognize(const Image& cropImage) {
#ifdef AI_HAS_TENSORRT
    // The ONNX model input is uint8 NHWC [N, 64, 128, 3].
    // It internally does Cast(uint8->fp32) → Mul(×1/255) → Transpose(NHWC→NCHW).
    // The TensorRT engine has dtype=uint8 on the input binding, so we must feed
    // raw uint8 bytes in NHWC layout via Tensor.raw.
    int H = cfg_.inputHeight ? cfg_.inputHeight : 64;
    int W = cfg_.inputWidth  ? cfg_.inputWidth  : 128;
    int C = 3;
    auto engine = engine_;
    const std::string inName = engine->bindings().empty() ? "input" : engine->bindings()[0].name;

    cv::Mat resized;
    cv::resize(cropImage.mat, resized, cv::Size(W, H), 0, 0, cv::INTER_LINEAR);

    // Build uint8 NHWC tensor.
    Tensor t;
    t.name = inName;
    t.shape = {1, H, W, C};
    t.raw.resize(static_cast<size_t>(H) * W * C);

    for (int y = 0; y < H; ++y) {
        const uchar* row = resized.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            const uchar* p = row + x * 3;  // OpenCV BGR
            size_t dst = (static_cast<size_t>(y) * W + x) * 3;
            if (cfg_.colorOrder == 1) {  // BGR -> RGB
                t.raw[dst + 0] = p[2];
                t.raw[dst + 1] = p[1];
                t.raw[dst + 2] = p[0];
            } else {  // keep BGR
                t.raw[dst + 0] = p[0];
                t.raw[dst + 1] = p[1];
                t.raw[dst + 2] = p[2];
            }
        }
    }

    std::vector<Tensor> ins{t}, outs;
    engine_->execute(0, ins, outs, 0);
    if (outs.empty()) return PlateResult{};

    // Find the "plate" output [1, T, 37] — it's the one with 3 dims.
    // The other output is region [1, 66] which we skip.
    const std::vector<float>* d = nullptr;
    int T = 1, Cc = 1;
    for (const auto& o : outs) {
        if (o.shape.size() == 3) {
            d = &o.data;
            T = (int)o.shape[1];
            Cc = (int)o.shape[2];
            break;
        }
    }
    if (!d) {
        d = &outs.back().data;
        auto& s = outs.back().shape;
        if (s.size() == 3) { T = (int)s[1]; Cc = (int)s[2]; }
        else if (s.size() == 2) { T = (int)s[0]; Cc = (int)s[1]; }
    }

    PlateResult res;
    res.text = decodeSlots(*d, T, Cc, res.charConfidences);
    // A plate is only as trustworthy as its weakest character.
    res.confidence = res.charConfidences.empty()
        ? 0.f : *std::min_element(res.charConfidences.begin(), res.charConfidences.end());
    return res;
#else
    (void)cropImage;
    return PlateResult{};
#endif
}

std::string TrtTextRecognizer::decodeSlots(const std::vector<float>& seq, int slots, int classes,
                                           std::vector<float>& charConf) {
    // The plate head is a fixed-slot classifier: one softmax over the alphabet
    // (+ a trailing pad class) per character position. Slots are independent,
    // so repeated neighbours are real ("LEE", "9887") and must NOT be collapsed
    // as CTC would; only pad slots are skipped.
    const int pad = classes - 1;
    std::string out;
    charConf.clear();
    for (int t = 0; t < slots; ++t) {
        int best = 0; float bestV = -1e9f;
        for (int c = 0; c < classes; ++c) {
            float v = seq[(size_t)t * classes + c];
            if (v > bestV) { bestV = v; best = c; }
        }
        if (best == pad || best >= (int)cfg_.alphabet.size()) continue;
        out.push_back(cfg_.alphabet[best]);
        charConf.push_back(bestV);
    }
    return out;
}

std::string TrtTextRecognizer::name() const { return "trt-ocr"; }

// ---------------------------------------------------------------------------
// Face embedding.
// ---------------------------------------------------------------------------

TrtFaceEmbedder::TrtFaceEmbedder(std::shared_ptr<TrtEngine> engine, ModelConfig cfg)
    : engine_(std::move(engine)), cfg_(std::move(cfg)) {}

Embedding TrtFaceEmbedder::embed(const Image& faceCrop) {
#ifdef AI_HAS_TENSORRT
    int W = cfg_.inputWidth ? cfg_.inputWidth : 160;
    int H = cfg_.inputHeight ? cfg_.inputHeight : 160;
    cv::Mat resized;
    cv::resize(faceCrop.mat, resized, cv::Size(W, H), 0, 0, cv::INTER_AREA);

    Tensor t;
    t.name = engine_->bindings().empty() ? "input" : engine_->bindings()[0].name;
    t.shape = {1, 3, H, W};
    t.data.assign(static_cast<size_t>(1) * 3 * H * W, 0.f);
    // facenet-pytorch standardization (px - 127.5) / 128; measured better than px / 255.
    const float mean = 127.5f, invStd = 1.f / 128.f;
    size_t plane = static_cast<size_t>(H) * W;
    for (int y = 0; y < H; ++y) {
        const uchar* row = resized.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            size_t idx = static_cast<size_t>(y) * W + x;
            const uchar* px = row + x * 3;
            t.data[0 * plane + idx] = (px[2] - mean) * invStd;
            t.data[1 * plane + idx] = (px[1] - mean) * invStd;
            t.data[2 * plane + idx] = (px[0] - mean) * invStd;
        }
    }

    std::vector<Tensor> ins{t}, outs;
    engine_->execute(0, ins, outs, 0);
    if (outs.empty()) return Embedding{};

    Embedding emb;
    emb.data = outs[0].data;
    emb.dim = (int)emb.data.size();
    // L2 normalize
    float norm = 0.f;
    for (float v : emb.data) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-9f) for (auto& v : emb.data) v /= norm;
    emb.normalized = true;
    emb.dim = (int)emb.data.size();
    return emb;
#else
    (void)faceCrop;
    return Embedding{};
#endif
}

std::string TrtFaceEmbedder::name() const { return "trt-embedder"; }

int TrtFaceEmbedder::embeddingDim() const {
    return cfg_.embeddingDim > 0 ? cfg_.embeddingDim : 128;
}

} // namespace ai
