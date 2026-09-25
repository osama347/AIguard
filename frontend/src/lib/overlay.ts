import type { FaceDetection, PlateDetection } from "../api/client";

/**
 * Draws face/plate boxes over media shown with object-fit: contain.
 * Sizes the canvas to its element and clears it first. `srcW/srcH` is the
 * coordinate space of the boxes (the analysed frame).
 */
export function drawDetections(
  canvas: HTMLCanvasElement,
  srcW: number,
  srcH: number,
  faces: FaceDetection[],
  plates: PlateDetection[],
) {
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  if (canvas.width !== Math.round(rect.width * dpr) || canvas.height !== Math.round(rect.height * dpr)) {
    canvas.width = Math.round(rect.width * dpr);
    canvas.height = Math.round(rect.height * dpr);
  }
  const ctx = canvas.getContext("2d")!;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, rect.width, rect.height);
  if (!srcW || !srcH) return;

  // Media is letterboxed inside its element.
  const scale = Math.min(rect.width / srcW, rect.height / srcH);
  const ox = (rect.width - srcW * scale) / 2, oy = (rect.height - srcH * scale) / 2;
  const css = getComputedStyle(document.documentElement);
  const color = (name: string) => css.getPropertyValue(name).trim();

  const box = (b: number[], stroke: string, label: string) => {
    const [x, y, w, h] = b;
    const X = ox + x * scale, Y = oy + y * scale;
    ctx.strokeStyle = stroke;
    ctx.lineWidth = 2;
    ctx.strokeRect(X, Y, w * scale, h * scale);
    if (!label) return;
    ctx.font = "600 12px system-ui, sans-serif";
    const tw = ctx.measureText(label).width + 10;
    const ly = Y > 20 ? Y - 20 : Y + h * scale;
    ctx.fillStyle = stroke;
    ctx.fillRect(X, ly, tw, 20);
    ctx.fillStyle = "#fff";
    ctx.fillText(label, X + 5, ly + 14);
  };

  for (const face of faces) {
    if (face.driver_name) box(face.bbox, color("--ok"), `${face.driver_name} ${Math.round((face.similarity ?? 0) * 100)}%`);
    else box(face.bbox, color("--bad"), "Unknown");
  }
  for (const p of plates) box(p.bbox, p.vehicle_id ? color("--ok") : color("--plate"), p.text || "plate");
}
