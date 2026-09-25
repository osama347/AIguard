import { useCallback, useEffect, useRef, useState, type ReactNode } from "react";
import { Link } from "react-router-dom";
import { api, subscribe, type Dashboard } from "../api/client";
import { CAMERA_STATE, formatBytes, formatRelative, STATUS_TONE } from "../lib/format";
import { VerdictColumns, VerdictLegend, VerdictTable } from "../components/VerdictColumns";
import { Badge, ErrorBox, PageHeader, Spinner } from "../components/ui";

// ------------------------------------------------------------------ formatting

const hourTick = (b: string) => b.slice(11, 13);
const hourTitle = (b: string) => {
  const h = Number(b.slice(11, 13));
  const day = b.slice(0, 10) === localDate(new Date()) ? "Today" : "Yesterday";
  return `${day} ${String(h).padStart(2, "0")}:00–${String((h + 1) % 24).padStart(2, "0")}:00`;
};
const dayDate = (b: string) => new Date(`${b}T12:00:00`);
const dayTick = (b: string) => dayDate(b).toLocaleDateString(undefined, { day: "numeric" });
const dayTitle = (b: string) => dayDate(b).toLocaleDateString(undefined, { weekday: "long", day: "numeric", month: "short" });
function localDate(d: Date) {
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}

function formatSeconds(s: number | null | undefined) {
  if (s === null || s === undefined) return "—";
  if (s < 60) return `${Math.round(s)} s`;
  if (s < 3600) return `${Math.floor(s / 60)} min ${Math.round(s % 60)} s`;
  return `${Math.floor(s / 3600)} h ${Math.round((s % 3600) / 60)} min`;
}

function formatUptime(s: number) {
  if (s < 3600) return `${Math.round(s / 60)} min`;
  if (s < 86400) return `${Math.floor(s / 3600)} h ${Math.round((s % 3600) / 60)} min`;
  return `${Math.floor(s / 86400)} d ${Math.round((s % 86400) / 3600)} h`;
}

// ------------------------------------------------------------------ building blocks

function Tile({ label, value, sub, icon, tone, to }: {
  label: string; value: ReactNode; sub?: ReactNode; icon?: string; tone?: "ok" | "bad" | "warn" | "muted"; to?: string;
}) {
  const body = (
    <>
      <div className="tile-label">
        {icon && <span className={`tile-icon tile-icon-${tone ?? "muted"}`} aria-hidden>{icon}</span>}
        {label}
      </div>
      <div className="tile-value">{value}</div>
      {sub && <div className="tile-sub">{sub}</div>}
    </>
  );
  return to ? <Link to={to} className="card stat-tile stat-link">{body}</Link> : <div className="card stat-tile">{body}</div>;
}

function Delta({ now, before }: { now: number; before: number }) {
  if (now === before) return <span className="muted">same as yesterday</span>;
  const up = now > before;
  return <span>{up ? "▲" : "▼"} {Math.abs(now - before)} vs yesterday</span>;
}

function ChartCard({ title, subtitle, children, table }: { title: string; subtitle?: string; children: ReactNode; table: ReactNode }) {
  const [asTable, setAsTable] = useState(false);
  return (
    <section className="card">
      <div className="card-head">
        <div>
          <h2>{title}</h2>
          {subtitle && <div className="muted small">{subtitle}</div>}
        </div>
        <div className="seg" role="group" aria-label="View">
          <button className={!asTable ? "on" : ""} onClick={() => setAsTable(false)}>Chart</button>
          <button className={asTable ? "on" : ""} onClick={() => setAsTable(true)}>Table</button>
        </div>
      </div>
      {asTable ? table : children}
    </section>
  );
}

// ------------------------------------------------------------------ page

export function DashboardPage() {
  const [data, setData] = useState<Dashboard | null>(null);
  const [error, setError] = useState<Error | null>(null);
  const [updated, setUpdated] = useState<Date | null>(null);
  const pending = useRef<number | undefined>(undefined);

  const load = useCallback(() => {
    api.dashboard().then((d) => { setData(d); setError(null); setUpdated(new Date()); }).catch(setError);
  }, []);

  useEffect(() => {
    load();
    const t = setInterval(load, 30000);
    return () => clearInterval(t);
  }, [load]);

  // New visits/alerts: refresh soon (debounced, several can arrive together).
  useEffect(() => subscribe((ev) => {
    if (ev.type === "access.event" || ev.type.startsWith("alert.") || ev.type === "camera.status") {
      window.clearTimeout(pending.current);
      pending.current = window.setTimeout(load, 1500);
    }
  }), [load]);

  const registerPlate = async (plate: string) => {
    if (!confirm(`Register vehicle ${plate}? You can add make, model and drivers afterwards on the Vehicles page.`)) return;
    try { await api.createVehicle({ plate_number: plate }); load(); } catch (e) { alert((e as Error).message); }
  };
  const ack = async (id: number) => {
    try { await api.acknowledgeAlert(id); load(); } catch (e) { alert((e as Error).message); }
  };

  if (!data) return (
    <>
      <PageHeader title="Dashboard" />
      <ErrorBox error={error} onRetry={load} />
      {!error && <Spinner />}
    </>
  );

  const t = data.today, y = data.yesterday;
  const unknown = t.unknown_driver + t.unknown_vehicle + t.unknown_both;
  const authRate = t.total ? Math.round((t.authorized / t.total) * 100) : null;
  const a = data.alerts, sys = data.system;
  const camsDown = data.cameras_enabled - data.cameras_live;
  const diskUsed = sys.disk_total_bytes - sys.disk_free_bytes;
  const diskPct = sys.disk_total_bytes ? diskUsed / sys.disk_total_bytes : 0;
  const attention = data.cameras.filter((c) => c.enabled && c.status.state !== "live");

  return (
    <>
      <PageHeader
        title="Dashboard"
        subtitle={<>Live cameras only · updated {updated?.toLocaleTimeString(undefined, { timeStyle: "short" })}</>}
        actions={<button className="btn" onClick={load}>Refresh</button>}
      />
      <ErrorBox error={error} onRetry={load} />

      <div className="stat-grid">
        <Tile label="Visits today" value={t.total.toLocaleString()} sub={<Delta now={t.total} before={y.total} />} to="/access" />
        <Tile label="Authorized" icon="✓" tone="ok" value={t.authorized.toLocaleString()}
              sub={authRate === null ? "no visits yet" : `${authRate}% of visits`} />
        <Tile label="Unauthorized" icon="✕" tone={t.unauthorized ? "bad" : "muted"} value={t.unauthorized.toLocaleString()}
              sub="wrong driver or blacklisted" />
        <Tile label="Unknown" icon="?" tone={unknown ? "warn" : "muted"} value={unknown.toLocaleString()}
              sub={`${t.unknown_driver} driver · ${t.unknown_vehicle} vehicle · ${t.unknown_both} neither`} />
        <Tile label="Open alerts" icon="!" tone={a.open_critical ? "bad" : a.open ? "warn" : "ok"} value={a.open.toLocaleString()}
              sub={a.open ? `${a.open_critical} critical · ${a.unacknowledged} not acknowledged` : "all clear"} to="/alerts" />
        <Tile label="Guard response" value={formatSeconds(a.median_response_s)}
              sub={a.responses_7d ? `median of ${a.responses_7d} alert${a.responses_7d === 1 ? "" : "s"}, 7 days` : "no alerts handled in 7 days"} />
        <Tile label="Cameras live" icon={camsDown ? "!" : "✓"} tone={camsDown ? "bad" : data.cameras_enabled ? "ok" : "muted"}
              value={<>{data.cameras_live}<span className="tile-of"> / {data.cameras_enabled}</span></>}
              sub={camsDown ? `${camsDown} not live` : data.cameras_enabled ? "all running" : "none configured"} to="/cameras" />
      </div>

      <div className="dash-grid">
        <div className="stack">
          <ChartCard title="Visits, last 24 hours" subtitle="Each column is one hour, split by decision."
                     table={<VerdictTable data={data.hourly} title={hourTitle} />}>
            <VerdictLegend />
            <VerdictColumns data={data.hourly} tick={hourTick} title={hourTitle} tickEvery={3} ariaLabel="Visits per hour over the last 24 hours" />
          </ChartCard>
          <ChartCard title="Visits, last 14 days" table={<VerdictTable data={data.daily} title={dayTitle} />}>
            <VerdictLegend />
            <VerdictColumns data={data.daily} tick={dayTick} title={dayTitle} ariaLabel="Visits per day over the last 14 days" />
          </ChartCard>
        </div>

        <div className="stack">
          <section className="card">
            <div className="card-head"><h2>Needs attention</h2><Link to="/alerts" className="small">All alerts →</Link></div>
            {!a.recent_open.length && !attention.length && !data.unregistered_plates.length && (
              <div className="muted small">Nothing right now.</div>
            )}
            {attention.map((c) => (
              <div key={c.id} className="attn">
                <Badge tone="bad">Camera</Badge>
                <div className="attn-text"><strong>{c.name}</strong><span className="muted small">{c.status.message || CAMERA_STATE[c.status.state]?.label}</span></div>
              </div>
            ))}
            {a.recent_open.map((al) => (
              <div key={al.id} className="attn">
                <Badge tone={STATUS_TONE[al.severity]}>{al.severity}</Badge>
                <div className="attn-text">
                  <span>{al.message}</span>
                  <span className="muted small">{formatRelative(al.created_at)}{al.acknowledged_by ? ` · acknowledged by ${al.acknowledged_by}` : ""}</span>
                </div>
                {!al.acknowledged_at && <button className="btn btn-small" onClick={() => ack(al.id)}>Acknowledge</button>}
              </div>
            ))}
          </section>

          <section className="card">
            <div className="card-head"><h2>Unregistered plates</h2><span className="muted small">last 7 days</span></div>
            {!data.unregistered_plates.length ? (
              <div className="muted small">Every plate seen belongs to a registered vehicle.</div>
            ) : (
              <table className="compact">
                <tbody>
                  {data.unregistered_plates.map((p) => (
                    <tr key={p.plate}>
                      <td className="mono"><strong>{p.plate}</strong></td>
                      <td className="num">{p.count}×</td>
                      <td className="muted small">{formatRelative(p.last_seen)}{p.camera ? ` · ${p.camera}` : ""}</td>
                      <td className="num"><button className="btn btn-small" onClick={() => registerPlate(p.plate)}>Register</button></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </section>

          <section className="card">
            <div className="card-head"><h2>Cameras</h2><Link to="/cameras" className="small">Manage →</Link></div>
            {!data.cameras.length ? <div className="muted small">No cameras yet. <Link to="/cameras">Add one</Link>.</div> : (
              <table className="compact">
                <tbody>
                  {data.cameras.map((c) => {
                    const st = CAMERA_STATE[c.status.state] ?? { label: c.status.state, tone: "muted" as const };
                    return (
                      <tr key={c.id}>
                        <td><strong>{c.name}</strong></td>
                        <td><Badge tone={st.tone}>{st.label}</Badge></td>
                        <td className="num small">{c.status.state === "live" ? `${c.status.analysis_fps.toFixed(1)} fps` : "—"}</td>
                        <td className="num small">{c.visits_today} today</td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            )}
          </section>
        </div>
      </div>

      <div className="dash-grid">
        <section className="card">
          <div className="card-head"><h2>Frequent drivers</h2><span className="muted small">last 7 days</span></div>
          {!data.top_drivers.length ? <div className="muted small">No recognized drivers yet.</div> : (
            <table className="compact">
              <thead><tr><th>Driver</th><th className="num">Visits</th><th className="num">Authorized</th></tr></thead>
              <tbody>
                {data.top_drivers.map((d) => (
                  <tr key={d.driver_id}>
                    <td>{d.name}</td>
                    <td className="num">{d.visits}</td>
                    <td className="num">{d.authorized === d.visits ? <Badge tone="ok">all</Badge> : `${d.authorized} of ${d.visits}`}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </section>

        <section className="card">
          <div className="card-head"><h2>System</h2><span className="muted small">v{sys.version} · up {formatUptime(sys.uptime_s)}</span></div>
          <dl className="facts">
            <dt>AI engine</dt>
            <dd><Badge tone={sys.inference.status === "ok" ? "ok" : sys.inference.status === "loading" ? "warn" : "bad"}>
              {sys.inference.status === "ok" ? "ready" : sys.inference.status}</Badge>
              {sys.inference.status !== "ok" && sys.inference.message && <span className="muted small"> {sys.inference.message}</span>}</dd>
            <dt>Disk</dt>
            <dd>
              <div className="meter" role="meter" aria-valuenow={Math.round(diskPct * 100)} aria-valuemin={0} aria-valuemax={100}
                   aria-label="Disk used"><div style={{ width: `${diskPct * 100}%` }} className={diskPct > 0.9 ? "meter-bad" : ""} /></div>
              <span className="small">{formatBytes(sys.disk_free_bytes)} free of {formatBytes(sys.disk_total_bytes)}</span>
            </dd>
            <dt>Snapshots</dt><dd>{formatBytes(sys.snapshot_bytes)} <span className="muted small">(kept {sys.snapshot_retention_days} days)</span></dd>
            <dt>Database</dt><dd>{formatBytes(sys.database_bytes)}</dd>
            <dt>Users</dt><dd>{sys.active_guards} guard{sys.active_guards === 1 ? "" : "s"}, {sys.active_admins} admin{sys.active_admins === 1 ? "" : "s"} <Link to="/users" className="small">Manage →</Link></dd>
          </dl>
        </section>
      </div>
    </>
  );
}
