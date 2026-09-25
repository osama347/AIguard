#pragma once
#include "ai/capabilities.h"
#include "ai/trt_engine.h"
#include "ai/config.h"
#include <memory>

namespace ai {

// ---------------------------------------------------------------------------
// TensorRT-backed model implementations.
//
// Each class owns:
//   * a TrtEngine (composition)
//   * its own TensorRT binding semantics
//   * its own preprocessing + postprocessing
//
// This is where "you can swap best.engine for something else" is realized:
// these classes depend only on the capability interface + config, and the
// only thing that changes between models is enginePath + per-model params.
// ---------------------------------------------------------------------------

// --- YOLO-style detector (plates and faces share this implementation) ----
class TrtYoloDetector : public IDetector {
public:
    // labels = class names for this detector (e.g. {"plate"} or {"face"}).
    TrtYoloDetector(std::shared_ptr<TrtEngine> engine, ModelConfig cfg,
                    std::string displayName);
    std::vector<Detection> detect(const Image& image) override;
    std::string name() const override { return displayName_; }

private:
    // letterbox -> RGB float NCHW into a Tensor
    Tensor preprocess(const cv::Mat& frame);
    std::vector<Detection> decodeOutputs(const Tensor& out,
                                         int origW, int origH,
                                         float confThr, float nmsThr);

    std::shared_ptr<TrtEngine> engine_;
    ModelConfig cfg_;
    std::string displayName_;
    int outW_ = 0, outH_ = 0;     // resolved letterbox dims
};

// --- OCR / text recognition (CRNN/PARSeq/etc.) --------------------------
class TrtTextRecognizer : public ITextRecognizer {
public:
    TrtTextRecognizer(std::shared_ptr<TrtEngine> engine, ModelConfig cfg);
    PlateResult recognize(const Image& cropImage) override;
    std::string name() const override;

private:
    std::shared_ptr<TrtEngine> engine_;
    ModelConfig cfg_;
    // fixed-slot decode: argmax per slot, pad skipped, per-character probabilities out
    std::string decodeSlots(const std::vector<float>& seq, int slots, int classes,
                            std::vector<float>& charConf);
};

// --- Face embedding (FaceNet/ArcFace/etc.) ------------------------------
class TrtFaceEmbedder : public IEmbeddingModel {
public:
    TrtFaceEmbedder(std::shared_ptr<TrtEngine> engine, ModelConfig cfg);
    Embedding embed(const Image& faceCrop) override;
    std::string name() const override;
    int embeddingDim() const override;
    bool normalizedOutput() const override { return true; }

private:
    std::shared_ptr<TrtEngine> engine_;
    ModelConfig cfg_;
};

} // namespace ai
