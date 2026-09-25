#include "ai/perception.h"
#include "ai/model_manager.h"
#include "ai/timing.h"
#include <algorithm>
#include <stdexcept>

namespace ai {

namespace {

// Clip a detection box to the frame; empty rect if nothing remains.
cv::Rect clip(const BBox& b, const cv::Mat& frame) {
    return cv::Rect(b.x, b.y, b.w, b.h) & cv::Rect(0, 0, frame.cols, frame.rows);
}

// Face crop for the embedder: the detector box grown by 15% of its size per side (measured
// in tools/face_eval.py). Enrollment and live matching must use the same crop.
constexpr float kFaceCropMargin = 0.15f;
cv::Rect faceCrop(const BBox& b, const cv::Mat& frame) {
    const int mx = int(b.w * kFaceCropMargin), my = int(b.h * kFaceCropMargin);
    return cv::Rect(b.x - mx, b.y - my, b.w + 2 * mx, b.h + 2 * my) & cv::Rect(0, 0, frame.cols, frame.rows);
}

Image view(const cv::Mat& m) {
    Image img;
    img.mat = m;
    return img;
}

} // namespace

FramePerception Perception::analyze(const cv::Mat& frame) {
    std::lock_guard<std::mutex> lk(gpu_);
    if (!models_.ready()) throw std::runtime_error("models not ready");

    FramePerception out;
    const Image img = view(frame);

    Stopwatch sw;
    auto faces = models_.faceDetector()->detect(img);
    out.timingsMs["face_detect_ms"] = sw.elapsedMs();

    sw.reset();
    for (const auto& d : faces) {
        FaceObservation fo;
        fo.bbox = d.bbox;
        fo.confidence = d.confidence;
        cv::Rect roi = faceCrop(d.bbox, frame);
        if (!roi.empty()) fo.embedding = models_.faceEmbedder()->embed(view(frame(roi))).data;
        out.faces.push_back(std::move(fo));
    }
    out.timingsMs["face_embed_ms"] = sw.elapsedMs();

    sw.reset();
    auto plates = models_.plateDetector()->detect(img);
    out.timingsMs["plate_detect_ms"] = sw.elapsedMs();

    sw.reset();
    for (const auto& d : plates) {
        PlateObservation po;
        po.bbox = d.bbox;
        po.confidence = d.confidence;
        cv::Rect roi = clip(d.bbox, frame);
        if (!roi.empty()) {
            PlateResult r = models_.ocr()->recognize(view(frame(roi)));
            po.text = r.text;
            po.textConfidence = r.confidence;
            po.charConfidences = r.charConfidences;
        }
        out.plates.push_back(std::move(po));
    }
    out.timingsMs["ocr_ms"] = sw.elapsedMs();
    return out;
}

std::optional<FaceObservation> Perception::bestFace(const cv::Mat& photo) {
    std::lock_guard<std::mutex> lk(gpu_);
    if (!models_.ready()) throw std::runtime_error("models not ready");

    auto faces = models_.faceDetector()->detect(view(photo));
    if (faces.empty()) return std::nullopt;
    const auto& best = *std::max_element(faces.begin(), faces.end(),
        [](const Detection& a, const Detection& b) { return a.confidence < b.confidence; });
    cv::Rect roi = faceCrop(best.bbox, photo);
    if (roi.empty()) return std::nullopt;

    FaceObservation fo;
    fo.bbox = best.bbox;
    fo.confidence = best.confidence;
    fo.embedding = models_.faceEmbedder()->embed(view(photo(roi))).data;
    if (fo.embedding.empty()) return std::nullopt;
    return fo;
}

} // namespace ai
