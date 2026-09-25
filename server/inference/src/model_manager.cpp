#include "ai/model_manager.h"
#include "ai/engine_resolver.h"
#include "ai/logging.h"
#include <nlohmann/json.hpp>
#include <thread>

#ifdef AI_HAS_TENSORRT
#include "ai/trt_engine.h"
#include "ai/trt_models.h"
#endif

namespace ai {

using json = nlohmann::json;

void ModelManager::loadAsync(const AppConfig& cfg) {
    std::thread([this, cfg] { load(cfg); }).detach();
}

void ModelManager::setStatus(State s, const std::string& msg) {
    std::lock_guard<std::mutex> lk(mtx_);
    state_ = s;
    message_ = msg;
}

ModelManager::State ModelManager::state() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return state_;
}

std::string ModelManager::message() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return message_;
}

void ModelManager::load(AppConfig cfg) {
#ifdef AI_HAS_TENSORRT
    try {
        EngineResolver resolver(cfg.tensorrt);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            trtVersion_ = resolver.trtVersion();
            device_ = resolver.device();
            precision_ = cfg.tensorrt.precision;
        }

        auto openEngine = [&](const std::string& key) -> std::pair<std::shared_ptr<TrtEngine>, ModelConfig> {
            auto it = cfg.models.find(key);
            if (it == cfg.models.end()) throw std::runtime_error("model '" + key + "' missing from config");
            setStatus(State::Loading, "preparing " + key);
            std::string path = resolver.resolve(key, it->second,
                                                [this](const std::string& m) { setStatus(State::Loading, m); });
            TrtEngine::Spec spec;
            spec.enginePath = path;
            spec.deviceId = cfg.tensorrt.deviceId;
            spec.useFp16 = cfg.tensorrt.precision == "fp16";
            auto eng = std::make_shared<TrtEngine>(std::move(spec));
            eng->load();
            if (cfg.tensorrt.warmup) eng->warmUp();
            {
                std::lock_guard<std::mutex> lk(mtx_);
                loaded_.push_back(key);
            }
            LOG_INFO << "Loaded " << key << ": " << path;
            return {eng, it->second};
        };

        auto [plateEng, plateCfg] = openEngine("plate_detector");
        plate_ = std::make_shared<TrtYoloDetector>(plateEng, plateCfg, "plate_detector");
        auto [faceEng, faceCfg] = openEngine("face_detector");
        face_ = std::make_shared<TrtYoloDetector>(faceEng, faceCfg, "face_detector");
        auto [ocrEng, ocrCfg] = openEngine("ocr");
        ocr_ = std::make_shared<TrtTextRecognizer>(ocrEng, ocrCfg);
        auto [embEng, embCfg] = openEngine("face_embedding");
        embedder_ = std::make_shared<TrtFaceEmbedder>(embEng, embCfg);

        setStatus(State::Ready, "models ready");
        LOG_INFO << "All models ready";
    } catch (const std::exception& e) {
        LOG_ERROR << "Model loading failed: " << e.what();
        setStatus(State::Error, e.what());
    }
#else
    (void)cfg;
    setStatus(State::Error, "this build has no TensorRT support (rebuild with -DAI_USE_TENSORRT=ON)");
#endif
}

std::string ModelManager::statusJson() const {
    std::lock_guard<std::mutex> lk(mtx_);
    static const char* names[] = {"loading", "ready", "error"};
    json j{{"state", names[static_cast<int>(state_)]},
           {"message", message_},
           {"models", loaded_},
           {"precision", precision_},
           {"trt_version", trtVersion_},
           {"device", device_}};
    return j.dump();
}

} // namespace ai
