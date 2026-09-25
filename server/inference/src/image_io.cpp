#include "ai/image_io.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>

namespace ai {

std::optional<cv::Mat> decodeImageBytes(const std::vector<char>& bytes) {
    if (bytes.empty()) return std::nullopt;
    cv::Mat raw(1, (int)bytes.size(), CV_8UC1, const_cast<char*>(bytes.data()));
    try {
        cv::Mat img = cv::imdecode(raw, cv::IMREAD_COLOR);
        if (img.empty()) return std::nullopt;
        return img;
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<char> encodeImage(const cv::Mat& mat, const std::string& ext) {
    std::vector<uchar> buf;
    cv::imencode(ext, mat, buf);
    return std::vector<char>(buf.begin(), buf.end());
}

double imageMegapixels(const cv::Mat& mat) {
    if (mat.empty()) return 0.0;
    return (static_cast<double>(mat.cols) * mat.rows) / 1e6;
}

// --- Image methods declared in types.h ----------------------------------
bool Image::empty() const {
#ifdef AI_HAS_OPENCV
    return mat.empty();
#else
    return true;
#endif
}
int Image::width() const {
#ifdef AI_HAS_OPENCV
    return mat.cols;
#else
    return 0;
#endif
}
int Image::height() const {
#ifdef AI_HAS_OPENCV
    return mat.rows;
#else
    return 0;
#endif
}

float Embedding::length() const {
    double s = 0.0;
    for (float v : data) s += (double)v * v;
    return (float)std::sqrt(s);
}

} // namespace ai
