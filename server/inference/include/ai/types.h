#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <map>

#ifdef AI_HAS_OPENCV
#include <opencv2/opencv.hpp>
#endif

namespace ai {

// ---------------------------------------------------------------------------
// Bounding box in pixel coordinates (x, y, w, h) on the input image.
// ---------------------------------------------------------------------------
struct BBox {
    int x = 0, y = 0, w = 0, h = 0;

    [[nodiscard]] float area() const { return static_cast<float>(w) * static_cast<float>(h); }
};

// ---------------------------------------------------------------------------
// A box produced by a detector, with label + confidence.
// ---------------------------------------------------------------------------
struct Detection {
    BBox bbox;
    float confidence = 0.f;
    std::string label;
};

// ---------------------------------------------------------------------------
// Result of recognizing text from a crop (e.g. license plate OCR).
// ---------------------------------------------------------------------------
struct PlateResult {
    std::string text;
    float confidence = 0.f;             // weakest character's probability
    std::vector<float> charConfidences; // one per character in `text`
    BBox bbox;
};

// ---------------------------------------------------------------------------
// Encapsulates either a raw vector (embedding) or a normalized vector.
// ---------------------------------------------------------------------------
struct Embedding {
    std::vector<float> data;
    int dim = 0;
    bool normalized = false;

    [[nodiscard]] float length() const;
};

// ---------------------------------------------------------------------------
// The universal carrier for an image. In MVP this is a CPU cv::Mat; the rest
// of the system passes this around and never re-copies it except at stage
// boundaries that require it (H2D transfer).
// ---------------------------------------------------------------------------
struct Image {
#ifdef AI_HAS_OPENCV
    cv::Mat mat;                 // BGR, CPU. The pipeline currency.
#endif
    std::string requestId;

    [[nodiscard]] bool empty() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
};

// ---------------------------------------------------------------------------
// Generic 1-D or (N,C,H,W) tensor used to move host data in/out of engines.
// ---------------------------------------------------------------------------
struct Tensor {
    std::vector<float> data;        // contiguous host data (row-major) for fp32 tensors
    std::vector<uint8_t> raw;       // contiguous host data for integer (uint8/int8) tensors
    std::vector<int64_t> shape;
    std::string name;
};

} // namespace ai
