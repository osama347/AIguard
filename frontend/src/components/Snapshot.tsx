import { useRef, useState } from "react";
import { api, type AccessEvent } from "../api/client";
import { drawDetections } from "../lib/overlay";

/** Snapshot of a camera visit with its detection boxes. */
export function Snapshot({ event, className }: { event: AccessEvent; className?: string }) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [failed, setFailed] = useState(false);
  const overlay = event.details?.overlay;
  if (!event.has_snapshot || failed) return <div className={`snapshot snapshot-none ${className ?? ""}`}>No picture</div>;

  const redraw = () => {
    if (canvasRef.current && overlay) drawDetections(canvasRef.current, overlay.width, overlay.height, overlay.faces, overlay.plates);
  };
  return (
    <div className={`snapshot ${className ?? ""}`}>
      <img src={api.snapshotUrl(event.id)} alt="Snapshot" loading="lazy" onLoad={redraw} onError={() => setFailed(true)} />
      <canvas ref={canvasRef} />
    </div>
  );
}
