#include "app/frame_annotator.h"
#include "common/base64.h"
#include "domain/plate.h"

using json = nlohmann::json;

namespace guard {

namespace {
constexpr std::size_t kMinPlateLength = 3;   // shorter OCR reads are noise, not plates
}

AnnotatedFrame annotateFrame(const json& ev, const FleetSnapshot& fleet) {
    AnnotatedFrame out;
    FrameObservation& obs = out.observation;
    obs.frame = ev.value("frame", 0ull);
    obs.tsMs = ev.value("ts", static_cast<int64_t>(ev.value("t_ms", 0.0)));
    obs.hasImage = ev.value("image", false);

    for (const auto& f : ev.value("faces", json::array())) {
        const float conf = f.value("conf", 0.f);
        json o{{"bbox", f["bbox"]}, {"conf", conf}};
        std::optional<FaceMatch> m;
        if (f.contains("embedding") && fleet.matcher)
            m = fleet.matcher->match(base64ToEmbed(f["embedding"].get<std::string>()));
        if (m) {
            o["driver_id"] = m->driverId;
            o["driver_name"] = m->name;
            o["similarity"] = m->similarity;
        }
        obs.faces.push_back(m);
        obs.faceConfidences.push_back(conf);
        out.faces.push_back(std::move(o));
    }

    for (const auto& p : ev.value("plates", json::array())) {
        const std::string norm = normalizePlate(p.value("text", ""));
        PlateObservation po;
        po.confidence = p.value("conf", 0.f);
        json o{{"bbox", p["bbox"]}, {"conf", po.confidence}, {"text", norm}, {"text_conf", p.value("text_conf", 0.0)}};
        if (norm.size() >= kMinPlateLength) {
            po.text = norm;
            auto it = fleet.vehicles.find(norm);
            if (it != fleet.vehicles.end()) {
                po.vehicleId = it->second.id;
                po.vehicleStatus = it->second.status;
                o["vehicle_id"] = it->second.id;
            }
        }
        obs.plates.push_back(std::move(po));
        out.plates.push_back(std::move(o));
    }
    obs.overlayJson = json{{"width", ev.value("width", 0)}, {"height", ev.value("height", 0)},
                           {"faces", out.faces}, {"plates", out.plates}}.dump();
    return out;
}

} // namespace guard
