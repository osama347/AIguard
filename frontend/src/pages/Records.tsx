import { useEffect, useState } from "react";
import { Link } from "react-router-dom";
import { api, subscribe, type AccessEvent, type Verdict } from "../api/client";
import { useAsync } from "../lib/useAsync";
import { useSession } from "../session";
import { formatBytes, formatRelative, formatTime, percent, STATUS_TONE, VERDICTS } from "../lib/format";
import { cn } from "../lib/cn";
import { Badge, Button, buttonClass, Card, Empty, ErrorBox, PageHeader, Select, Spinner, Switch } from "../components/ui";
import { Snapshot } from "../components/Snapshot";
import { EventModal } from "./Live";

/** Radix Select forbids "" as an item value; this stands in for "no filter". */
const ALL = "__all__";

export function JobsPage() {
  const { data: jobs, error, loading, reload } = useAsync(() => api.jobs(200), []);

  // Refresh when any job changes state.
  useEffect(() => subscribe((ev) => {
    if (ev.type.startsWith("job.") && ev.type !== "job.frame") reload();
  }), [reload]);

  return (
    <>
      <PageHeader title="Test videos" subtitle="Uploaded clips analysed for testing, newest first. Live cameras are on the Live page." actions={<Link to="/analyze" className={buttonClass("primary")}>Test a video</Link>} />
      <ErrorBox error={error} onRetry={reload} />
      {loading && !jobs ? <div className="grid place-items-center py-16"><Spinner /></div> : !jobs?.length ? (
        <Empty title="No test videos yet"><Link to="/analyze" className="text-accent">Upload a clip</Link> to check the models and rules.</Empty>
      ) : (
        <Card flush>
          <div className="overflow-x-auto">
            <table>
              <thead><tr><th>Video</th><th>Result</th><th>Uploaded</th><th>Size</th><th>By</th></tr></thead>
              <tbody>
                {jobs.map((j) => (
                  <tr key={j.id}>
                    <td><Link to={`/jobs/${j.id}`} className="break-words">{j.original_name || j.id}</Link></td>
                    <td>
                      {j.verdict ? <Badge tone={VERDICTS[j.verdict].tone}>{VERDICTS[j.verdict].label}</Badge>
                       : <Badge tone={STATUS_TONE[j.status]}>{j.status === "running" ? `running ${percent(j.progress)}` : j.status}</Badge>}
                    </td>
                    <td className="whitespace-nowrap">{formatTime(j.created_at)}</td>
                    <td className="whitespace-nowrap">{formatBytes(j.size_bytes)}</td>
                    <td>{j.created_by}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </Card>
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
      <div className="mb-4 flex flex-wrap gap-2.5">
        {isAdmin && (
          <Select value={source || ALL} onValueChange={(v) => { setSource(v === ALL ? "" : v as typeof source); setCameraId(""); }}
                  options={[{ value: ALL, label: "All sources" }, { value: "camera", label: "Live cameras" }, { value: "video", label: "Test videos" }]} />
        )}
        {source !== "video" && !!cameras?.length && (
          <Select value={cameraId || ALL} onValueChange={(v) => setCameraId(v === ALL ? "" : v)}
                  options={[{ value: ALL, label: "All cameras" }, ...cameras.map((c) => ({ value: String(c.id), label: c.name }))]} />
        )}
        <Select value={verdict || ALL} onValueChange={(v) => setVerdict(v === ALL ? "" : v as typeof verdict)}
                options={[{ value: ALL, label: "All decisions" }, ...(Object.keys(VERDICTS) as Verdict[]).map((v) => ({ value: v, label: VERDICTS[v].label }))]} />
      </div>
      <ErrorBox error={error} onRetry={reload} />
      {loading && !events ? <div className="grid place-items-center py-16"><Spinner /></div> : !events?.length ? (
        <Empty title="No access events">They appear here as vehicles pass the cameras, or after a test video is analysed.</Empty>
      ) : (
        <Card flush>
          <div className="overflow-x-auto">
            <table>
              <thead><tr><th /><th>Time</th><th>Decision</th><th>Driver</th><th>Plate</th><th>Source</th><th>Reason</th></tr></thead>
              <tbody>
                {events.map((e) => (
                  <tr key={e.id} className="cursor-pointer" onClick={() => setSelected(e)}>
                    <td className="w-[88px] py-1.5">{e.source === "camera" ? <Snapshot event={e} className="snapshot-mini" /> : null}</td>
                    <td className="whitespace-nowrap">{formatTime(e.event_time)}</td>
                    <td><Badge tone={VERDICTS[e.verdict]?.tone ?? "muted"}>{VERDICTS[e.verdict]?.label ?? e.verdict}</Badge></td>
                    <td>{e.driver_name ?? "—"}</td>
                    <td className="font-mono">{e.plate_text || "—"}</td>
                    <td className="whitespace-nowrap">
                      {e.source === "camera" ? (e.camera_name ?? "Removed camera")
                       : e.job_id && isAdmin ? <Link to={`/jobs/${e.job_id}`} onClick={(ev) => ev.stopPropagation()}>Test video</Link> : "Test video"}
                    </td>
                    <td className="text-muted">{e.details?.reason ?? ""}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </Card>
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
          <label className="flex items-center gap-2.5 text-[13px]">
            <Switch checked={openOnly} onCheckedChange={setOpenOnly} /> Open only
          </label>
        }
      />
      <ErrorBox error={error} onRetry={reload} />
      {loading && !alerts ? <div className="grid place-items-center py-16"><Spinner /></div> : !alerts?.length ? (
        <Empty title={openOnly ? "No open alerts" : "No alerts"}>All clear.</Empty>
      ) : (
        <ul className="m-0 flex list-none flex-col gap-2.5 p-0">
          {alerts.map((a) => (
            <li key={a.id} className={cn(
              "flex flex-wrap items-center justify-between gap-4 rounded-lg border border-line border-l-[3px] bg-surface px-4 py-3.5",
              a.severity === "critical" ? "border-l-bad" : "border-l-warn",
            )}>
              <div className="flex min-w-0 flex-col gap-1.5">
                <div className="flex flex-wrap items-center gap-2.5">
                  <Badge tone={STATUS_TONE[a.severity]}>{a.severity}</Badge>
                  <span className="text-[12px] text-muted">{formatTime(a.created_at)}</span>
                  {a.resolved_at && <Badge tone="ok">resolved</Badge>}
                  {!a.resolved_at && a.acknowledged_at && <Badge tone="muted">acknowledged</Badge>}
                </div>
                <div className="text-[13px]">{a.message}</div>
                {(a.acknowledged_by || a.resolved_by) && (
                  <div className="text-[12px] text-muted">
                    {a.acknowledged_by && <>Acknowledged by {a.acknowledged_by} {formatRelative(a.acknowledged_at)}</>}
                    {a.acknowledged_by && a.resolved_by && " · "}
                    {a.resolved_by && <>Resolved by {a.resolved_by} {formatRelative(a.resolved_at)}</>}
                  </div>
                )}
              </div>
              {!a.resolved_at && (
                <div className="flex flex-wrap gap-2">
                  {!a.acknowledged_at && <Button size="sm" onClick={() => act(() => api.acknowledgeAlert(a.id))}>Acknowledge</Button>}
                  <Button size="sm" onClick={() => act(() => api.resolveAlert(a.id))}>Resolve</Button>
                </div>
              )}
            </li>
          ))}
        </ul>
      )}
    </>
  );
}
