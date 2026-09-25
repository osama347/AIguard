#pragma once
#include "ai/config.h"
#include "ai/capabilities.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ai {

// ---------------------------------------------------------------------------
// ModelManager: owns every model instance; loads once, stays resident.
//
// Loading can take minutes when engines must be built from ONNX, so it runs
// on a background thread (loadAsync) while the HTTP server already answers
// /v1/health with the current state and progress message.
//
// Model keys (config "models"): plate_detector, face_detector, ocr,
// face_embedding. All four are required for the service to be ready.
// ---------------------------------------------------------------------------
class ModelManager {
public:
    enum class State { Loading, Ready, Error };

    void loadAsync(const AppConfig& cfg);

    // Non-owning accessors; valid only when state() == Ready.
    IDetector* plateDetector()      { return plate_.get(); }
    IDetector* faceDetector()       { return face_.get(); }
    ITextRecognizer* ocr()          { return ocr_.get(); }
    IEmbeddingModel* faceEmbedder() { return embedder_.get(); }

    State state() const;
    bool ready() const { return state() == State::Ready; }
    std::string message() const;
    std::string statusJson() const;   // for /v1/models and /v1/health

private:
    void load(AppConfig cfg);
    void setStatus(State s, const std::string& msg);

    std::shared_ptr<IDetector>       plate_;
    std::shared_ptr<IDetector>       face_;
    std::shared_ptr<ITextRecognizer> ocr_;
    std::shared_ptr<IEmbeddingModel> embedder_;

    mutable std::mutex mtx_;
    State state_ = State::Loading;
    std::string message_ = "starting";
    std::vector<std::string> loaded_;
    std::string trtVersion_, device_, precision_;
};

} // namespace ai
