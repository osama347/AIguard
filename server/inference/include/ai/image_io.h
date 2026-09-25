#pragma once
#include "ai/types.h"
#include <string>
#include <optional>

namespace ai {

// ---------------------------------------------------------------------------
// Image encode/decode + boundary helpers. Decoding happens exactly ONCE at
// the HTTP boundary; after that the cv::Mat is passed (ROI views) through the
// whole pipeline without further deep copies.
// ---------------------------------------------------------------------------

// Decode raw JPEG/PNG bytes into a BGR cv::Mat. Returns nullopt if the bytes
// do not decode to a valid image.
std::optional<cv::Mat> decodeImageBytes(const std::vector<char>& bytes);

// Encode a cv::Mat back to a given format (e.g. ".jpg"). Used by any
// future endpoint that returns an image (e.g. annotated crops).
std::vector<char> encodeImage(const cv::Mat& mat, const std::string& ext);

// Estimate the image area in megapixels (used for size-limit checks).
double imageMegapixels(const cv::Mat& mat);

} // namespace ai
