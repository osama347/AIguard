import { useEffect, useRef, useState } from "react";
import type { Frame } from "../api/client";
import { drawDetections } from "../lib/overlay";

interface Props {
  src: string;
  frames: Frame[];               // sorted by t_ms
  width?: number;                // source video size (for box scaling)
  height?: number;
  holdMs?: number;               // how long a frame's boxes stay visible
  autoPlay?: boolean;
  image?: boolean;               // src is a still picture, not a clip
}

/** Latest analysed frame at or before t (binary search), if still fresh. */
function frameAt(frames: Frame[], tMs: number, holdMs: number): Frame | null {
  let lo = 0, hi = frames.length - 1, best = -1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (frames[mid].t_ms <= tMs) { best = mid; lo = mid + 1; } else hi = mid - 1;
  }
  if (best < 0) return null;
  return tMs - frames[best].t_ms <= holdMs ? frames[best] : null;
}

export function VideoOverlay({ src, frames, width, height, holdMs = 400, autoPlay, image }: Props) {
  const imgRef = useRef<HTMLImageElement>(null);
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const framesRef = useRef(frames);
  framesRef.current = frames;
  const [unplayable, setUnplayable] = useState(false);
  useEffect(() => setUnplayable(false), [src]);

  // A picture has one analysed frame: draw its boxes once the picture and results are in.
  const drawStill = () => {
    const img = imgRef.current, canvas = canvasRef.current;
    if (!img || !canvas || !img.naturalWidth) return;
    const f = framesRef.current[0];
    drawDetections(canvas, width || img.naturalWidth, height || img.naturalHeight, f?.faces ?? [], f?.plates ?? []);
  };
  useEffect(() => { if (image) drawStill(); }, [image, frames, width, height]);

  useEffect(() => {
    if (image) return;
    let raf = 0;
    const draw = () => {
      raf = requestAnimationFrame(draw);
      const video = videoRef.current, canvas = canvasRef.current;
      if (!video || !canvas) return;
      const f = frameAt(framesRef.current, video.currentTime * 1000, holdMs);
      drawDetections(canvas, width || video.videoWidth, height || video.videoHeight, f?.faces ?? [], f?.plates ?? []);
    };
    draw();
    return () => cancelAnimationFrame(raf);
  }, [image, width, height, holdMs]);

  return (
    <div className="stage">
      {image ? (
        <img ref={imgRef} src={src} alt="Uploaded test picture" onLoad={drawStill} />
      ) : (
        <video ref={videoRef} src={src} controls muted playsInline autoPlay={autoPlay}
               onError={() => setUnplayable(true)} onLoadedData={() => setUnplayable(false)} />
      )}
      <canvas ref={canvasRef} />
      {unplayable && (
        <div className="stage-message">
          This video's format can't be played here. The analysis results are unaffected;
          re-encode the clip as H.264 MP4 to watch it with overlays.
        </div>
      )}
    </div>
  );
}
