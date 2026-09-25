#pragma once
#include "ai/types.h"

namespace ai {

// ---------------------------------------------------------------------------
// Capability interfaces. These are the ONLY virtual layer in the system.
//
// A pipeline depends on a *capability* (detection, recognition, embedding),
// never on a concrete TensorRT model file. Model implementations implement
// these interfaces; swapping models never touches pipelines or HTTP.
// ---------------------------------------------------------------------------

// Base interface for any bounding-box detector (plates, faces, objects, ...).
class IDetector {
public:
    virtual ~IDetector() = default;
    // Detect objects in a BGR cv::Mat frame. Returns boxes sorted by
    // descending confidence, filtered by the detector's configured threshold.
    virtual std::vector<Detection> detect(const Image& image) = 0;
    // Human/machine readable model identity for status endpoint.
    virtual std::string name() const = 0;
};

// Text recognition from a single cropped image (license plates, OCR).
class ITextRecognizer {
public:
    virtual ~ITextRecognizer() = default;
    virtual PlateResult recognize(const Image& cropImage) = 0;
    virtual std::string name() const = 0;
};

// Produces a face embedding / feature vector from a face crop.
class IEmbeddingModel {
public:
    virtual ~IEmbeddingModel() = default;
    virtual Embedding embed(const Image& faceCrop) = 0;
    virtual std::string name() const = 0;
    // If false, the embedding is returned unnormalized; the matcher must
    // normalize internally. If true, embed() returns L2-normalized vectors.
    virtual bool normalizedOutput() const { return false; }
    virtual int embeddingDim() const = 0;
};

} // namespace ai
