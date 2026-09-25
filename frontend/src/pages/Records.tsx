import { useEffect, useState } from "react";
import { Link } from "react-router-dom";
import { api, subscribe, type AccessEvent, type Verdict } from "../api/client";
import { useAsync } from "../lib/useAsync";
import { useSession } from "../session";
import { formatBytes, formatRelative, formatTime, percent, STATUS_TONE, VERDICTS } from "../lib/format";
import { Badge, Empty, ErrorBox, PageHeader, Spinner } from "../components/ui";
import { Snapshot } from "../components/Snapshot";
import { EventModal } from "./Live";

export function JobsPage() {
  const { data: jobs, error, loading, reload } = useAsync(() => api.jobs(200), []);

  // Refresh when any job changes state.
  useEffect(() => subscribe((ev) => {
    if (ev.type.startsWith("job.") && ev.type !== "job.frame") reload();
  }), [reload]);

  return (
    <>
      <PageHeader title="Test videos" subtitle="Uploaded clips analysed for testing, newest first. Live cameras are on the Live page." actions={<Link className="btn btn-primary" to="/analyze">Test a video</Link>} />
      <ErrorBox error={error} onRetry={reload} />
      {loading && !jobs ? <Spinner /> : !jobs?.length ? (
        <Empty title="No test videos yet"><Link to="/analyze">Upload a clip</Link> to check the models and rules.</Empty>
      ) : (
        <div className="card card-flush table-wrap">
          <table>
            <thead><tr><th>Video</th><th>Result</th><th>Uploaded</th><th>Size</th><th>By</th></tr></thead>
            <tbody>
              {jobs.map((j) => (
                <tr key={j.id}>
                  <td><Link to={`/jobs/${j.id}`} className="break">{j.original_name || j.id}</Link></td>
                  <td>
                    {j.verdict ? <Badge tone={VERDICTS[j.verdict].tone}>{VERDICTS[j.verdict].label}</Badge>
                     : <Badge tone={STATUS_TONE[j.status]}>{j.status === "running" ? `running ${percent(j.progress)}` : j.status}</Badge>}
                  </td>
                  <td className="nowrap">{formatTime(j.created_at)}</td>
                  <td className="nowrap">{formatBytes(j.size_bytes)}</td>
                  <td>{j.created_by}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </>
  );
}

export function AccessLogPage() {
  const { isAdmin } = useSession();
  const [source, setSource] = useState<"" | "camera" | "video">("");
  const [cameraId, setCameraId] = useState<string>("");
  const [verdict, setVerdict] = useState<"" | Verdict>("");
  const [selected, setSelected] = useState<AccessEvent | null>(null);
  const { data: cameras } = useAsync(() => api.cameras(), []);
  const { data: events, error, loading, reload } = useAsync(
    () => api.accessEvents(300, 0, {
      source: source || undefined,
      camera_id: cameraId ? Number(cameraId) : undefined,
      verdict: verdict || undefined,
    }),
    [source, cameraId, verdict]);
  useEffect(() => subscribe((ev) => { if (ev.type === "access.event") reload(); }), [reload]);

  return (
    <>
      <PageHeader title="Access log" subtitle={isAdmin ? "Every decision: vehicles and people seen by the cameras, and test videos." : "Every vehicle and person seen by the cameras."} />
      <div className="row filters">
        {isAdmin && (
          <select value={source} onChange={(e) => { setSource(e.target.value as typeof source); setCameraId(""); }}>
            <option value="">All sources</option>
            <option value="camera">Live cameras</option>
            <option value="video">Test videos</option>
          </select>
        )}
        {source !== "video" && !!cameras?.length && (
          <select value={cameraId} onChange={(e) => setCameraId(e.target.value)}>
            <option value="">All cameras</option>
            {cameras.map((c) => <option key={c.id} value={c.id}>{c.name}</option>)}
          </select>
        )}
        <select value={verdict} onChange={(e) => setVerdict(e.target.value as typeof verdict)}>
          <option value="">All decisions</option>
          {(Object.keys(VERDICTS) as Verdict[]).map((v) => <option key={v} value={v}>{VERDICTS[v].label}</option>)}
        </select>
      </div>
      <ErrorBox error={error} onRetry={reload} />
      {loading && !events ? <Spinner /> : !events?.length ? (
        <Empty title="No access events">They appear here as vehicles pass the cameras, or after a test video is analysed.</Empty>
      ) : (
        <div className="card card-flush table-wrap">
          <table>
            <thead><tr><th /><th>Time</th><th>Decision</th><th>Driver</th><th>Plate</th><th>Source</th><th>Reason</th></tr></thead>
            <tbody>
              {events.map((e) => (
                <tr key={e.id} className="clickable" onClick={() => setSelected(e)}>
                  <td className="thumb-cell">{e.source === "camera" ? <Snapshot event={e} className="snapshot-mini" /> : null}</td>
                  <td className="nowrap">{formatTime(e.event_time)}</td>
                  <td><Badge tone={VERDICTS[e.verdict]?.tone ?? "muted"}>{VERDICTS[e.verdict]?.label ?? e.verdict}</Badge></td>
                  <td>{e.driver_name ?? "—"}</td>
                  <td className="mono">{e.plate_text || "—"}</td>
                  <td className="nowrap">
                    {e.source === "camera" ? (e.camera_name ?? "Removed camera")
                     : e.job_id && isAdmin ? <Link to={`/jobs/${e.job_id}`} onClick={(ev) => ev.stopPropagation()}>Test video</Link> : "Test video"}
                  </td>
                  <td className="muted">{e.details?.reason ?? ""}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
      {selected && <EventModal event={selected} onClose={() => setSelected(null)} />}
    </>
  );
}

export function AlertsPage() {
  const { refreshAlerts } = useSession();
  const [openOnly, setOpenOnly] = useState(true);
  const { data: alerts, error, loading, reload } = useAsync(() => api.alerts(openOnly), [openOnly]);
  useEffect(() => subscribe((ev) => { if (ev.type.startsWith("alert.")) reload(); }), [reload]);

  const act = async (fn: () => Promise<unknown>) => {
    try { await fn(); reload(); refreshAlerts(); } catch (e) { alert((e as Error).message); }
  };

  return (
    <>
      <PageHeader
        title="Alerts"
        subtitle="Unauthorized or suspicious access attempts, and cameras that went offline."
        actions={
          <label className="toggle">
            <input type="checkbox" checked={openOnly} onChange={(e) => setOpenOnly(e.target.checked)} /> Open only
          </label>
        }
      />
      <ErrorBox error={error} onRetry={reload} />
      {loading && !alerts ? <Spinner /> : !alerts?.length ? (
        <Empty title={openOnly ? "No open alerts" : "No alerts"}>All clear.</Empty>
      ) : (
        <ul className="alert-list">
          {alerts.map((a) => (
            <li key={a.id} className={`card alert-item alert-${a.severity}`}>
              <div className="stack-tight">
                <div className="row">
                  <Badge tone={STATUS_TONE[a.severity]}>{a.severity}</Badge>
                  <span className="muted small">{formatTime(a.created_at)}</span>
                  {a.resolved_at && <Badge tone="ok">resolved</Badge>}
                  {!a.resolved_at && a.acknowledged_at && <Badge tone="muted">acknowledged</Badge>}
                </div>
                <div>{a.message}</div>
                {(a.acknowledged_by || a.resolved_by) && (
                  <div className="muted small">
                    {a.acknowledged_by && <>Acknowledged by {a.acknowledged_by} {formatRelative(a.acknowledged_at)}</>}
                    {a.acknowledged_by && a.resolved_by && " · "}
                    {a.resolved_by && <>Resolved by {a.resolved_by} {formatRelative(a.resolved_at)}</>}
                  </div>
                )}
              </div>
              {!a.resolved_at && (
                <div className="row">
                  {!a.acknowledged_at && <button className="btn btn-small" onClick={() => act(() => api.acknowledgeAlert(a.id))}>Acknowledge</button>}
                  <button className="btn btn-small" onClick={() => act(() => api.resolveAlert(a.id))}>Resolve</button>
                </div>
              )}
            </li>
          ))}
        </ul>
      )}
    </>
  );
}
