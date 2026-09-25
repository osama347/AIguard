import { useEffect, useRef, useState } from "react";
import type { VerdictCounts } from "../api/client";

/** Stack order, bottom to top. Chosen so similar hues (the two "unknown" warnings) never touch. */
export const VERDICT_SERIES = [
  { key: "authorized", label: "Authorized", icon: "✓" },
  { key: "unknown_driver", label: "Unknown driver", icon: "?" },
  { key: "unknown_both", label: "Nothing recognized", icon: "–" },
  { key: "unknown_vehicle", label: "Unknown vehicle", icon: "?" },
  { key: "unauthorized", label: "Unauthorized", icon: "✕" },
] as const;
export type VerdictKey = (typeof VERDICT_SERIES)[number]["key"];

const color = (k: VerdictKey) => `var(--v-${k.split("_").join("-")})`;

/** Legend shared by every verdict chart: swatch + label (identity is never color alone). */
export function VerdictLegend() {
  return (
    <div className="viz-legend">
      {VERDICT_SERIES.map((s) => (
        <span key={s.key}><i className="viz-swatch" style={{ background: color(s.key) }} />{s.label}</span>
      ))}
    </div>
  );
}

function niceStep(max: number, ticks = 4) {
  if (max <= 0) return 1;
  const raw = max / ticks;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const n = raw / mag;
  return (n <= 1 ? 1 : n <= 2 ? 2 : n <= 5 ? 5 : 10) * mag;
}

interface Props {
  data: VerdictCounts[];
  tick: (bucket: string) => string;        // short x label
  title: (bucket: string) => string;       // tooltip heading
  tickEvery?: number;                      // label every n-th column
  height?: number;
  ariaLabel: string;
}

/** Stacked columns of visits per time bucket, split by verdict, with a per-column tooltip. */
export function VerdictColumns({ data, tick, title, tickEvery = 1, height = 210, ariaLabel }: Props) {
  const wrap = useRef<HTMLDivElement>(null);
  const [width, setWidth] = useState(600);
  const [hover, setHover] = useState<number | null>(null);

  useEffect(() => {
    const el = wrap.current;
    if (!el) return;
    const ro = new ResizeObserver(([e]) => setWidth(Math.max(240, e.contentRect.width)));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const pad = { l: 34, r: 6, t: 8, b: 22 };
  const plotW = width - pad.l - pad.r, plotH = height - pad.t - pad.b;
  const maxTotal = Math.max(0, ...data.map((d) => d.total));
  const step = niceStep(Math.max(maxTotal, 4));
  const yMax = Math.max(step, Math.ceil(maxTotal / step) * step);
  const y = (v: number) => pad.t + plotH - (v / yMax) * plotH;
  const band = plotW / Math.max(1, data.length);
  const colW = Math.max(3, Math.min(28, band * 0.62));
  const GAP = 2, RADIUS = 4;
  const ticks: number[] = [];
  for (let v = 0; v <= yMax + 1e-9; v += step) ticks.push(v);

  const columns = data.map((d, i) => {
    const x = pad.l + i * band + (band - colW) / 2;
    const segs: { key: VerdictKey; y: number; h: number; top: boolean }[] = [];
    let base = y(0);
    const present = VERDICT_SERIES.filter((s) => (d[s.key] ?? 0) > 0);
    present.forEach((s, j) => {
      const hpx = (d[s.key] / yMax) * plotH;
      const top = j === present.length - 1;
      // 2px surface gap above every segment except the topmost.
      const h = Math.max(1, top ? hpx : hpx - GAP);
      segs.push({ key: s.key, y: base - hpx, h, top });
      base -= hpx;
    });
    return { x, segs, d };
  });

  const topPath = (x: number, yTop: number, w: number, h: number) => {
    const r = Math.min(RADIUS, w / 2, h);
    return `M${x},${yTop + h}V${yTop + r}Q${x},${yTop} ${x + r},${yTop}H${x + w - r}Q${x + w},${yTop} ${x + w},${yTop + r}V${yTop + h}Z`;
  };

  const hovered = hover !== null ? data[hover] : null;
  const tipLeft = hover !== null ? Math.min(Math.max(pad.l + hover * band + band / 2, 90), width - 90) : 0;

  return (
    <div className="viz" ref={wrap}>
      <svg width={width} height={height} role="img" aria-label={ariaLabel} onMouseLeave={() => setHover(null)}>
        {ticks.map((v) => (
          <g key={v}>
            <line x1={pad.l} x2={width - pad.r} y1={y(v)} y2={y(v)} className={v === 0 ? "viz-baseline" : "viz-grid"} />
            <text x={pad.l - 6} y={y(v)} className="viz-tick" textAnchor="end" dominantBaseline="middle">{v.toLocaleString()}</text>
          </g>
        ))}
        {hover !== null && <rect x={pad.l + hover * band} y={pad.t} width={band} height={plotH} className="viz-band" />}
        {columns.map((c, i) => (
          <g key={c.d.bucket}>
            {c.segs.map((s) => s.top
              ? <path key={s.key} d={topPath(c.x, s.y, colW, s.h)} fill={color(s.key)} />
              : <rect key={s.key} x={c.x} y={s.y + GAP} width={colW} height={s.h} fill={color(s.key)} />)}
            {i % tickEvery === 0 && (
              <text x={c.x + colW / 2} y={height - 6} className="viz-tick" textAnchor="middle">{tick(c.d.bucket)}</text>
            )}
            {/* Hit target: the whole column band, taller and wider than the mark. */}
            <rect x={pad.l + i * band} y={pad.t} width={band} height={plotH} fill="transparent"
                  tabIndex={0} aria-label={`${title(c.d.bucket)}: ${c.d.total} visits`}
                  onMouseEnter={() => setHover(i)} onFocus={() => setHover(i)} onBlur={() => setHover(null)} />
          </g>
        ))}
        {maxTotal === 0 && (
          <text x={pad.l + plotW / 2} y={pad.t + plotH / 2} className="viz-empty" textAnchor="middle">No visits in this period</text>
        )}
      </svg>
      {hovered && (
        <div className="viz-tip" style={{ left: tipLeft }}>
          <div className="viz-tip-title">{title(hovered.bucket)}</div>
          <div className="viz-tip-total">{hovered.total.toLocaleString()} visit{hovered.total === 1 ? "" : "s"}</div>
          {[...VERDICT_SERIES].reverse().filter((s) => hovered[s.key] > 0).map((s) => (
            <div key={s.key} className="viz-tip-row">
              <i className="viz-swatch" style={{ background: color(s.key) }} />
              <span>{s.label}</span>
              <strong>{hovered[s.key].toLocaleString()}</strong>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

/** Same data as a table (accessibility and exact values). */
export function VerdictTable({ data, title }: { data: VerdictCounts[]; title: (bucket: string) => string }) {
  return (
    <div className="table-wrap viz-table">
      <table>
        <thead>
          <tr><th>Period</th>{VERDICT_SERIES.map((s) => <th key={s.key} className="num">{s.label}</th>)}<th className="num">Total</th></tr>
        </thead>
        <tbody>
          {[...data].reverse().map((d) => (
            <tr key={d.bucket}>
              <td className="nowrap">{title(d.bucket)}</td>
              {VERDICT_SERIES.map((s) => <td key={s.key} className="num">{d[s.key] || "—"}</td>)}
              <td className="num"><strong>{d.total}</strong></td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
