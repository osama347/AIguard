#pragma once
#include "ai/config.h"
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace ai {

class Perception;

// ---------------------------------------------------------------------------
// Camera source strings (what guard-core stores per camera):
//   csi://0[?width=1920&height=1080&fps=30]   Jetson CSI camera (nvarguscamerasrc)
//   usb://0[?width=1280&height=720]            USB/V4L2 camera /dev/video0
//   /dev/video0                                same as usb://0
//   rtsp://user:pass@host/path                 IP camera (hardware decode)
//   http(s)://...                              MJPEG / HLS over HTTP (FFmpeg)
//   file:///path/clip.mp4                      video file looped in real time (testing)
//   gst://<pipeline ending in appsink>         custom GStreamer pipeline
// ---------------------------------------------------------------------------
struct OpenedSource {
    std::string backend;        // gstreamer | v4l2 | ffmpeg | file
    bool isFile = false;        // replay at the file's own frame rate, loop at the end
};
std::optional<OpenedSource> openSource(const std::string& source, cv::VideoCapture& cap, std::string& err);

// ---------------------------------------------------------------------------
// LiveStream: one camera analysed continuously.
//
//   capture thread   reads frames as fast as the camera delivers them and
//                    keeps only the newest (analysis never lags behind live),
//                    reconnects with backoff when the camera drops, and
//                    encodes preview JPEGs while someone is watching.
//   analysis thread  takes the newest frame sample_fps times per second and
//                    runs perception on it (sharing the GPU with other work).
//
// Events (JSON, sequence-numbered, bounded buffer for resume):
//   status {state: connecting|live|reconnecting, message}
//   frame  {frame, ts, width, height, faces[], plates[], timings, image}
// ---------------------------------------------------------------------------
class LiveStream {
public:
    LiveStream(std::string id, std::string source, double sampleFps, Perception& perception,
               const LiveConfig& cfg);
    ~LiveStream();

    void start();
    void stop();                               // joins both threads

    const std::string& id() const { return id_; }
    const std::string& source() const { return source_; }
    double sampleFps() const { return sampleFps_; }
    nlohmann::json info();

    // Event with sequence >= next (advancing next), waiting up to `timeout`.
    // If `next` fell out of the buffer it skips to the oldest kept event.
    std::string waitEvent(uint64_t& next, std::chrono::milliseconds timeout, bool& stopped);

    // Newest preview JPEG with sequence > after, waiting up to `timeout`.
    std::optional<std::pair<uint64_t, std::string>> preview(uint64_t after, std::chrono::milliseconds timeout);

    // JPEG of an analysed frame that had detections, if still kept.
    std::optional<std::string> frameImage(uint64_t frame);

private:
    void captureLoop();
    void analysisLoop();
    void push(nlohmann::json event);
    void setState(const std::string& state, const std::string& message);
    bool sleepFor(std::chrono::milliseconds d);   // false if stopping

    const std::string id_, source_;
    const double sampleFps_;
    Perception& perception_;
    const LiveConfig cfg_;

    std::atomic<bool> stop_{false};
    std::thread capture_, analysis_;

    std::mutex mtx_;
    std::condition_variable cv_;             // new frame, new event, new preview, stop
    // latest captured frame
    cv::Mat latest_;
    uint64_t captured_ = 0;
    std::chrono::system_clock::time_point latestAt_;
    int width_ = 0, height_ = 0;
    double captureFps_ = 0, analysisFps_ = 0;
    std::string state_ = "connecting", message_, backend_;
    // events
    std::deque<std::pair<uint64_t, std::string>> events_;
    uint64_t nextSeq_ = 0;
    // preview
    std::string previewJpeg_;
    uint64_t previewSeq_ = 0;
    std::chrono::steady_clock::time_point previewWanted_{};
    // analysed frames with detections
    std::deque<std::pair<uint64_t, std::string>> images_;
    uint64_t analysed_ = 0;
};

// ---------------------------------------------------------------------------
// StreamManager: the set of live streams, keyed by id (guard-core uses
// "cam-<camera id>"). ensure() is idempotent so core can re-register after
// either service restarts.
// ---------------------------------------------------------------------------
class StreamManager {
public:
    StreamManager(Perception& perception, LiveConfig cfg) : perception_(perception), cfg_(cfg) {}
    ~StreamManager();

    // Starts the stream, or restarts it if source/fps changed. Returns
    // {stream, created}. Throws std::length_error when max_streams is reached.
    std::pair<std::shared_ptr<LiveStream>, bool> ensure(const std::string& id, const std::string& source,
                                                        double sampleFps);
    bool remove(const std::string& id);
    std::shared_ptr<LiveStream> find(const std::string& id);
    nlohmann::json list();
    std::size_t count();

    // Opens a source and grabs one frame: {ok, backend, width, height, fps} or {ok:false, error}.
    static nlohmann::json probe(const std::string& source);

private:
    static void retire(std::shared_ptr<LiveStream> s);   // stop without blocking the caller

    Perception& perception_;
    LiveConfig cfg_;
    std::mutex mtx_;
    std::map<std::string, std::shared_ptr<LiveStream>> streams_;
};

} // namespace ai
