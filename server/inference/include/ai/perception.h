#pragma once
#include "ai/types.h"
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ai {

class ModelManager;

struct FaceObservation {
    BBox bbox;
    float confidence = 0.f;
    std::vector<float> embedding;   // empty if the crop could not be embedded
};

struct PlateObservation {
    BBox bbox;
    float confidence = 0.f;         // detector confidence
    std::string text;               // OCR result (may be empty)
    float textConfidence = 0.f;
    std::vector<float> charConfidences;   // one per character of `text`
};

struct FramePerception {
    std::vector<FaceObservation> faces;
    std::vector<PlateObservation> plates;
    std::map<std::string, double> timingsMs;
};

// ---------------------------------------------------------------------------
// Perception: what the models see in one image. No identities, no fleet
// data, no decisions — those belong to guard-core.
//
// Thread-safe: every call holds the GPU lock, so video jobs and enrollment
// requests interleave frame-by-frame instead of racing on shared TensorRT
// execution contexts.
// ---------------------------------------------------------------------------
class Perception {
public:
    explicit Perception(ModelManager& models) : models_(models) {}

    // Faces (detect + embed) and plates (detect + OCR) in a BGR frame.
    FramePerception analyze(const cv::Mat& frame);

    // The most confident face in a photo, embedded. nullopt if none found.
    std::optional<FaceObservation> bestFace(const cv::Mat& photo);

private:
    ModelManager& models_;
    std::mutex gpu_;
};

} // namespace ai
