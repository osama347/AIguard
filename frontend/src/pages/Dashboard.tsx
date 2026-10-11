import { useCallback, useEffect, useRef, useState, type ReactNode } from "react";
import { Link } from "react-router-dom";
import {
  ArrowsClockwise, BellRinging, CheckCircle, ChartBar, Question, Timer, VideoCamera, XCircle,
} from "@phosphor-icons/react";
import { api, subscribe, type Dashboard } from "../api/client";
import { CAMERA_STATE, formatBytes, formatRelative, STATUS_TONE, type Tone } from "../lib/format";
import { cn } from "../lib/cn";
import { VerdictColumns, VerdictLegend, VerdictTable } from "../components/VerdictColumns";
import { Badge, Button, Card, CardHead, ErrorBox, PageHeader, Segmented, Spinner } from "../components/ui";
import { RegisterVehicleWizard } from "./Fleet";

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

const METRIC_ICON: Record<Tone, string> = {
  ok: "bg-ok/10 text-ok",
  bad: "bg-bad/10 text-bad",
  warn: "bg-warn/12 text-warn",
  info: "bg-info/10 text-info",
  muted: "bg-surface-2 text-muted",
};

function Metric({ label, value, sub, icon: Icon, tone = "muted", to }: {
  label: string; value: ReactNode; sub?: ReactNode; icon: React.ComponentType<{ size?: number; weight?: "regular" | "duotone" }>;
  tone?: Tone; to?: string;
}) {
  const body = (
    <>
      <div className="flex items-start justify-between gap-3">
        <span className="text-[12.5px] font-medium text-muted">{label}</span>
        <span className={cn("grid size-7 shrink-0 place-items-center rounded-md", METRIC_ICON[tone])}><Icon size={15} weight="duotone" /></span>
      </div>
      <div className="mt-2.5 text-[26px] font-semibold leading-none tracking-tight tabular-nums">{value}</div>
      {sub && <div className="mt-1.5 text-[12px] text-muted">{sub}</div>}
    </>
  );
  const cls = "rounded-lg border border-line bg-surface p-4 transition-colors";
  return to
    ? <Link to={to} className={cn(cls, "hover:border-line-strong hover:bg-surface-2/60")}>{body}</Link>
    : <div className={cls}>{body}</div>;
}

function Delta({ now, before }: { now: number; before: number }) {
  if (now === before) return <span>same as yesterday</span>;
  const up = now > before;
  return <span>{up ? "▲" : "▼"} {Math.abs(now - before)} vs yesterday</span>;
}

function ChartCard({ title, subtitle, chart, table, rangeControl }: {
  title: string; subtitle?: string; chart: ReactNode; table: ReactNode; rangeControl?: ReactNode;
}) {
  const [view, setView] = useState<"chart" | "table">("chart");
  return (
    <Card>
      <CardHead
        title={title}
        sub={subtitle}
        actions={
          <div className="flex items-center gap-2">
            {rangeControl}
            <Segmented
              value={view}
              onChange={setView}
              options={[{ value: "chart", label: "Chart" }, { value: "table", label: "Table" }]}
            />
          </div>
        }
      />
      {view === "table" ? table : chart}
    </Card>
  );
}

function AttentionRow({ tone, tag, text, sub, action }: {
  tone: Tone; tag: string; text: ReactNode; sub: ReactNode; action?: ReactNode;
}) {
  return (
    <div className="flex items-start gap-3 border-b border-line py-2.5 last:border-b-0">
      <Badge tone={tone}>{tag}</Badge>
      <div className="flex min-w-0 flex-1 flex-col gap-0.5">
        <span className="text-[13px]">{text}</span>
        <span className="text-[12px] text-muted">{sub}</span>
      </div>
      {action}
    </div>
  );
}

function MiniTable({ head, children }: { head?: ReactNode; children: ReactNode }) {
  return (
    <div className="table-wrap">
      <table className="compact">
        {head && <thead>{head}</thead>}
        <tbody>{children}</tbody>
      </table>
    </div>
  );
}

// ------------------------------------------------------------------ page

export function DashboardPage() {
  const [data, setData] = useState<Dashboard | null>(null);
  const [error, setError] = useState<Error | null>(null);
  const [actionError, setActionError] = useState<Error | null>(null);
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

  const [range, setRange] = useState<"24h" | "14d">("24h");
  const [registeringPlate, setRegisteringPlate] = useState<string | null>(null);
  const ack = async (id: number) => {
    setActionError(null);
    try { await api.acknowledgeAlert(id); load(); } catch (e) { setActionError(e as Error); }
  };

  if (!data) return (
    <>
      <PageHeader title="Dashboard" />
      <ErrorBox error={error} onRetry={load} />
      {!error && <div className="grid place-items-center py-20"><Spinner label="Loading dashboard…" /></div>}
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
        actions={<Button onClick={load}><ArrowsClockwise size={15} /> Refresh</Button>}
      />
      <ErrorBox error={error} onRetry={load} />

      <div className="mb-3.5 grid grid-cols-2 gap-3.5 lg:grid-cols-4">
        <Metric label="Visits today" icon={ChartBar} value={t.total.toLocaleString()} sub={<Delta now={t.total} before={y.total} />} to="/access" />
        <Metric label="Authorized" icon={CheckCircle} tone="ok" value={t.authorized.toLocaleString()}
                sub={authRate === null ? "no visits yet" : `${authRate}% of visits`} />
        <Metric label="Unauthorized" icon={XCircle} tone={t.unauthorized ? "bad" : "muted"} value={t.unauthorized.toLocaleString()}
                sub="wrong driver or blacklisted" />
        <Metric label="Unknown" icon={Question} tone={unknown ? "warn" : "muted"} value={unknown.toLocaleString()}
                sub={`${t.unknown_driver} driver · ${t.unknown_vehicle} vehicle · ${t.unknown_both} neither`} />
        <Metric label="Open alerts" icon={BellRinging} tone={a.open_critical ? "bad" : a.open ? "warn" : "ok"} value={a.open.toLocaleString()}
                sub={a.open ? `${a.open_critical} critical · ${a.unacknowledged} not acknowledged` : "all clear"} to="/alerts" />
        <Metric label="Guard response" icon={Timer} value={formatSeconds(a.median_response_s)}
                sub={a.responses_7d ? `median of ${a.responses_7d} alert${a.responses_7d === 1 ? "" : "s"}, 7 days` : "no alerts handled in 7 days"} />
        <Metric label="Cameras live" icon={VideoCamera} tone={camsDown ? "bad" : data.cameras_enabled ? "ok" : "muted"}
                value={<>{data.cameras_live}<span className="text-[16px] font-medium text-faint"> / {data.cameras_enabled}</span></>}
                sub={camsDown ? `${camsDown} not live` : data.cameras_enabled ? "all running" : "none configured"} to="/cameras" />
      </div>

      <div className="grid grid-cols-1 items-start gap-3.5 lg:grid-cols-[minmax(0,1.7fr)_minmax(300px,1fr)]">
        <div className="flex min-w-0 flex-col gap-3.5">
          <ChartCard
            title={range === "24h" ? "Visits, last 24 hours" : "Visits, last 14 days"}
            subtitle={range === "24h" ? "Each column is one hour, split by decision." : "Each column is one day, split by decision."}
            rangeControl={
              <Segmented value={range} onChange={setRange}
                         options={[{ value: "24h", label: "24 hours" }, { value: "14d", label: "14 days" }]} />
            }
            table={<VerdictTable data={range === "24h" ? data.hourly : data.daily} title={range === "24h" ? hourTitle : dayTitle} />}
            chart={<>
              <VerdictLegend />
              {range === "24h"
                ? <VerdictColumns data={data.hourly} tick={hourTick} title={hourTitle} tickEvery={3} ariaLabel="Visits per hour over the last 24 hours" />
                : <VerdictColumns data={data.daily} tick={dayTick} title={dayTitle} ariaLabel="Visits per day over the last 14 days" />}
            </>} />
        </div>

        <div className="flex min-w-0 flex-col gap-3.5">
          <Card>
            <CardHead title="Needs attention" actions={<Link to="/alerts" className="text-[12px] text-muted hover:text-text">All alerts →</Link>} />
            <ErrorBox error={actionError} />
            {!a.recent_open.length && !attention.length && !data.unregistered_plates.length && (
              <div className="py-1 text-[13px] text-muted">Nothing right now.</div>
            )}
            {attention.map((c) => (
              <AttentionRow key={c.id} tone="bad" tag="Camera" text={<strong className="font-medium">{c.name}</strong>}
                            sub={c.status.message || CAMERA_STATE[c.status.state]?.label} />
            ))}
            {a.recent_open.map((al) => (
              <AttentionRow key={al.id} tone={STATUS_TONE[al.severity]} tag={al.severity} text={al.message}
                            sub={<>{formatRelative(al.created_at)}{al.acknowledged_by ? ` · acknowledged by ${al.acknowledged_by}` : ""}</>}
                            action={!al.acknowledged_at ? <Button size="sm" onClick={() => ack(al.id)}>Acknowledge</Button> : undefined} />
            ))}
          </Card>

          <Card>
            <CardHead title="Unregistered plates" sub="last 7 days" />
            {!data.unregistered_plates.length ? (
              <div className="py-1 text-[13px] text-muted">Every plate seen belongs to a registered vehicle.</div>
            ) : (
              <MiniTable>
                {data.unregistered_plates.map((p) => (
                  <tr key={p.plate}>
                    <td className="mono font-medium">{p.plate}</td>
                    <td className="num">{p.count}×</td>
                    <td className="text-[12px] text-muted">{formatRelative(p.last_seen)}{p.camera ? ` · ${p.camera}` : ""}</td>
                    <td className="num"><Button size="sm" onClick={() => setRegisteringPlate(p.plate)}>Register</Button></td>
                  </tr>
                ))}
              </MiniTable>
            )}
          </Card>

          <Card>
            <CardHead title="Cameras" actions={<Link to="/cameras" className="text-[12px] text-muted hover:text-text">Manage →</Link>} />
            {!data.cameras.length ? <div className="py-1 text-[13px] text-muted">No cameras yet. <Link to="/cameras" className="text-accent">Add one</Link>.</div> : (
              <MiniTable>
                {data.cameras.map((c) => {
                  const st = CAMERA_STATE[c.status.state] ?? { label: c.status.state, tone: "muted" as const };
                  return (
                    <tr key={c.id}>
                      <td className="font-medium">{c.name}</td>
                      <td><Badge tone={st.tone}>{st.label}</Badge></td>
                      <td className="num text-[12px] text-muted">{c.status.state === "live" ? `${c.status.analysis_fps.toFixed(1)} fps` : "—"}</td>
                      <td className="num text-[12px] text-muted">{c.visits_today} today</td>
                    </tr>
                  );
                })}
              </MiniTable>
            )}
          </Card>
        </div>
      </div>

      <div className="mt-3.5 grid grid-cols-1 items-start gap-3.5 lg:grid-cols-[minmax(0,1.7fr)_minmax(300px,1fr)]">
        <Card>
          <CardHead title="Frequent drivers" sub="last 7 days" />
          {!data.top_drivers.length ? <div className="py-1 text-[13px] text-muted">No recognized drivers yet.</div> : (
            <MiniTable head={<tr><th>Driver</th><th className="num">Visits</th><th className="num">Authorized</th></tr>}>
              {data.top_drivers.map((d) => (
                <tr key={d.driver_id}>
                  <td>{d.name}</td>
                  <td className="num">{d.visits}</td>
                  <td className="num">{d.authorized === d.visits ? <Badge tone="ok">all</Badge> : `${d.authorized} of ${d.visits}`}</td>
                </tr>
              ))}
            </MiniTable>
          )}
        </Card>

        <Card>
          <CardHead title="System" sub={`v${sys.version} · up ${formatUptime(sys.uptime_s)}`} />
          <dl className="facts">
            <dt>AI engine</dt>
            <dd className="flex items-center gap-2">
              <Badge tone={sys.inference.status === "ok" ? "ok" : sys.inference.status === "loading" ? "warn" : "bad"}>
                {sys.inference.status === "ok" ? "ready" : sys.inference.status}
              </Badge>
              {sys.inference.status !== "ok" && sys.inference.message && <span className="text-[12px] text-muted">{sys.inference.message}</span>}
            </dd>
            <dt>Disk</dt>
            <dd>
              <div className="h-1.5 max-w-[220px] overflow-hidden rounded-full bg-surface-3" role="meter" aria-valuenow={Math.round(diskPct * 100)} aria-valuemin={0} aria-valuemax={100} aria-label="Disk used">
                <div className={cn("h-full rounded-full", diskPct > 0.9 ? "bg-bad" : "bg-info")} style={{ width: `${diskPct * 100}%` }} />
              </div>
              <span className="text-[12px] text-muted">{formatBytes(sys.disk_free_bytes)} free of {formatBytes(sys.disk_total_bytes)}</span>
            </dd>
            <dt>Snapshots</dt><dd>{formatBytes(sys.snapshot_bytes)} <span className="text-[12px] text-muted">(kept {sys.snapshot_retention_days} days)</span></dd>
            <dt>Database</dt><dd>{formatBytes(sys.database_bytes)}</dd>
            <dt>Users</dt><dd>{sys.active_guards} guard{sys.active_guards === 1 ? "" : "s"}, {sys.active_admins} admin{sys.active_admins === 1 ? "" : "s"} <Link to="/users" className="text-[12px] text-muted hover:text-text">Manage →</Link></dd>
          </dl>
        </Card>
      </div>

      {registeringPlate && (
        <RegisterVehicleWizard initialPlate={registeringPlate} onClose={() => setRegisteringPlate(null)}
          onDone={() => { setRegisteringPlate(null); load(); }} />
      )}
    </>
  );
}
