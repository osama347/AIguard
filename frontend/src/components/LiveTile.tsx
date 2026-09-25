import { useEffect, useRef, useState, type MutableRefObject } from "react";
import { api, type Camera, type LiveFrame } from "../api/client";
import { CAMERA_STATE } from "../lib/format";
import { drawDetections } from "../lib/overlay";
import { Badge } from "./ui";

export type FrameStore = MutableRefObject<Map<number, { frame: LiveFrame; at: number }>>;

const HOLD_MS = 800;   // boxes stay this long after the last analysed frame

/** One camera: MJPEG live view with the latest detections drawn on top. */
export function LiveTile({ camera, frames, large, onClick }: {
  camera: Camera; frames: FrameStore; large?: boolean; onClick?: () => void;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const inViewRef = useRef<HTMLSpanElement>(null);
  const [nonce, setNonce] = useState(0);
  const [broken, setBroken] = useState(false);
  const status = camera.status;
  const live = status.state === "live";

  // Reconnect the picture whenever the camera (re)goes live.
  useEffect(() => { if (live) { setNonce((n) => n + 1); setBroken(false); } }, [live]);
  useEffect(() => {
    if (!broken) return;
    const t = setTimeout(() => { setBroken(false); setNonce((n) => n + 1); }, 3000);
    return () => clearTimeout(t);
  }, [broken]);

  useEffect(() => {
    let raf = 0;
    const draw = () => {
      raf = requestAnimationFrame(draw);
      const canvas = canvasRef.current;
      if (!canvas) return;
      const entry = frames.current.get(camera.id);
      const fresh = entry && performance.now() - entry.at < HOLD_MS ? entry.frame : null;
      drawDetections(canvas, fresh?.width ?? 0, fresh?.height ?? 0, fresh?.faces ?? [], fresh?.plates ?? []);
      // "In view": something is detected right now (updated here to avoid re-rendering at frame rate).
      const inView = !!fresh && (fresh.faces.length > 0 || fresh.plates.length > 0);
      if (inViewRef.current) inViewRef.current.style.visibility = inView ? "visible" : "hidden";
    };
    draw();
    return () => cancelAnimationFrame(raf);
  }, [camera.id, frames]);

  const st = CAMERA_STATE[status.state] ?? { label: status.state, tone: "muted" as const };
  return (
    <div className={`tile ${large ? "tile-large" : ""} ${onClick ? "tile-click" : ""}`} onClick={onClick}>
      {camera.enabled && !broken && (
        <img src={api.liveUrl(camera.id, nonce)} alt={`Live view of ${camera.name}`} onError={() => setBroken(true)} />
      )}
      <canvas ref={canvasRef} />
      <div className="tile-head">
        <span className="tile-name">{camera.name}</span>
        <Badge tone={st.tone}>{st.label}</Badge>
      </div>
      {live && <span ref={inViewRef} className="tile-inview" style={{ visibility: "hidden" }}>● In view</span>}
      {!live && <div className="tile-message">{status.message || st.label}</div>}
      {live && (
        <div className="tile-foot">
          {status.width}×{status.height} · {status.analysis_fps.toFixed(1)} fps analysed
        </div>
      )}
    </div>
  );
}
