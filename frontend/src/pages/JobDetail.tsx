import { useEffect, useMemo, useState } from "react";
import { Link, useNavigate, useParams } from "react-router-dom";
import { api, subscribe, type Frame, type Job, type VideoInfo } from "../api/client";
import { useSession } from "../session";
import { formatBytes, formatDuration, formatTime, percent, STATUS_TONE, VERDICTS, isImageName } from "../lib/format";
import { cn } from "../lib/cn";
import { Badge, Button, Card, Chip, Empty, ErrorBox, Fact, Facts, PageHeader, Progress, Spinner } from "../components/ui";
import { VideoOverlay } from "../components/VideoOverlay";

const FINISHED = ["completed", "failed", "cancelled"];

const VERDICT_BOX: Record<string, string> = {
  ok: "bg-ok/12 text-ok border-ok/22",
  bad: "bg-bad/12 text-bad border-bad/22",
  warn: "bg-warn/13 text-warn border-warn/22",
  info: "bg-info/12 text-info border-info/22",
  muted: "bg-surface-2 text-muted border-line",
};

export function JobDetailPage() {
  const { id = "" } = useParams();
  const navigate = useNavigate();
  const { user } = useSession();
  const [job, setJob] = useState<Job | null>(null);
  const [frames, setFrames] = useState<Frame[]>([]);
  const [video, setVideo] = useState<VideoInfo | null>(null);
  const [error, setError] = useState<Error | null>(null);

  // Initial state, then live updates until the job finishes.
  useEffect(() => {
    let closed = false;
    setFrames([]);
    setJob(null);
    api.job(id).then((j) => {
      if (closed) return;
      setJob(j);
      if (j.status === "completed") {
        api.frames(id).then((doc) => { if (!closed) { setFrames(doc.frames); setVideo(doc.video); } }).catch(() => {});
      }
    }).catch((e) => setError(e));

    const unsubscribe = subscribe((ev) => {
      switch (ev.type) {
        case "job.started":
          setJob(ev.data.job);
          setVideo(ev.data.video);
          setFrames([]);
          break;
        case "job.frame":
          setFrames((fs) => (fs.length && fs[fs.length - 1].frame >= ev.data.frame ? fs : [...fs, ev.data]));
          setJob((j) => (j ? { ...j, status: "running", progress: ev.data.progress } : j));
          break;
        case "job.queued":
        case "job.completed":
        case "job.failed":
        case "job.cancelled":
          setJob(ev.data);
          if (ev.type === "job.completed") api.frames(id).then((d) => { setFrames(d.frames); setVideo(d.video); }).catch(() => {});
          break;
      }
    }, { job: id });
    return () => { closed = true; unsubscribe(); };
  }, [id]);

  const latest = frames[frames.length - 1];
  const live = job && !FINISHED.includes(job.status);
  const sampleGap = useMemo(() => {
    if (frames.length < 2) return 400;
    return Math.min(1500, (frames[frames.length - 1].t_ms - frames[0].t_ms) / (frames.length - 1) * 1.5);
  }, [frames]);

  if (error) return <ErrorBox error={error} />;
  if (!job) return <div className="grid place-items-center py-16"><Spinner label="Loading job…" /></div>;

  const result = job.result;
  const verdict = job.verdict ? VERDICTS[job.verdict] : null;

  const cancel = async () => { try { setJob(await api.cancelJob(id)); } catch (e) { setError(e as Error); } };
  const remove = async () => {
    if (!confirm("Delete this job and its video?")) return;
    try { await api.deleteJob(id); navigate("/jobs"); } catch (e) { setError(e as Error); }
  };

  return (
    <>
      <PageHeader
        title={job.original_name || `Job ${job.id}`}
        subtitle={<>Uploaded {formatTime(job.created_at)} by {job.created_by} · {formatBytes(job.size_bytes)}</>}
        actions={
          <>
            {live && <Button onClick={cancel}>Cancel</Button>}
            {!live && user?.role === "admin" && <Button variant="danger" onClick={remove}>Delete</Button>}
          </>
        }
      />

      <div className="grid items-start gap-4 lg:grid-cols-[minmax(0,1.8fr)_minmax(280px,1fr)]">
        <Card flush>
          <VideoOverlay src={api.videoUrl(id)} image={isImageName(job.original_name)} frames={frames} width={video?.width} height={video?.height} holdMs={sampleGap} />
          <div className="flex flex-wrap gap-4 px-3.5 py-2.5 text-[12px] text-muted">
            <span className="inline-flex items-center gap-1.5"><span className="size-2.5 rounded-[3px] bg-ok" /> recognized</span>
            <span className="inline-flex items-center gap-1.5"><span className="size-2.5 rounded-[3px] bg-bad" /> unknown face</span>
            <span className="inline-flex items-center gap-1.5"><span className="size-2.5 rounded-[3px] bg-plate" /> unregistered plate</span>
          </div>
        </Card>

        <div className="flex flex-col gap-3.5">
          <Card>
            {job.status === "queued" && (
              <>
                <Badge tone="info">Queued</Badge>
                <p className="mt-2 text-[13px] text-muted">
                  {job.queue_position ? `${job.queue_position} job(s) ahead. ` : ""}Waiting for the AI engine…
                </p>
              </>
            )}
            {job.status === "running" && (
              <>
                <div className="mb-3 flex items-center justify-between">
                  <Badge tone="info">Analysing</Badge>
                  <span className="text-[13px] text-muted">{percent(job.progress)}</span>
                </div>
                <Progress value={job.progress} />
                <p className="mt-2 text-[12px] text-muted">{frames.length} frames analysed</p>
              </>
            )}
            {job.status === "failed" && <><Badge tone="bad">Failed</Badge><ErrorBox error={job.error ?? "Analysis failed"} /></>}
            {job.status === "cancelled" && <Badge tone="muted">Cancelled</Badge>}
            {job.status === "completed" && result && verdict && (
              <div className={cn("rounded-lg border px-4 py-3.5", VERDICT_BOX[verdict.tone])}>
                <div className="mb-1 text-[18px] font-semibold">{verdict.label}</div>
                <div className="text-text">{result.reason}</div>
              </div>
            )}
          </Card>

          {result && (
            <Card>
              <h2 className="mb-3 text-sm font-semibold">Evidence</h2>
              <Facts>
                <Fact label="Driver">
                  {result.driver ? <Link to="/fleet?tab=people">{result.driver.name}</Link> : "—"}
                  {result.driver && <span className="text-[12px] text-muted"> · {percent(result.driver.similarity)} match in {result.driver.frames} frame(s)</span>}
                </Fact>
                <Fact label="Vehicle">
                  {result.vehicle ? <Link to="/fleet">{result.vehicle.plate_number}</Link> : "—"}
                  {result.vehicle && <span className="text-[12px] text-muted"> · read {result.vehicle.reads}×</span>}
                </Fact>
                <Fact label="Faces">{result.faces_seen} seen, {result.unknown_faces} unknown</Fact>
                <Fact label="Frames">{result.frames_analysed} analysed in {formatDuration(result.processing_ms)}</Fact>
              </Facts>
              {result.plates_read.length > 0 && (
                <>
                  <h3 className="mb-2 mt-4 text-[11px] font-semibold uppercase tracking-wider text-faint">Plates read</h3>
                  <div className="flex flex-wrap gap-1.5">
                    {result.plates_read.map((p) => (
                      <Chip key={p.text} tone={p.vehicle_id ? "ok" : "muted"}>{p.text} ×{p.reads}</Chip>
                    ))}
                  </div>
                </>
              )}
              {result.drivers_seen.length > 1 && (
                <>
                  <h3 className="mb-2 mt-4 text-[11px] font-semibold uppercase tracking-wider text-faint">Drivers seen</h3>
                  <div className="flex flex-wrap gap-1.5">
                    {result.drivers_seen.map((d) => <Chip key={d.id}>{d.name} ×{d.frames}</Chip>)}
                  </div>
                </>
              )}
            </Card>
          )}

          {live && latest && (
            <Card>
              <h2 className="mb-1 text-sm font-semibold">Latest frame</h2>
              <p className="mb-3 text-[12px] text-muted">Frame {latest.frame} · {(latest.t_ms / 1000).toFixed(1)} s</p>
              {latest.faces.length === 0 && latest.plates.length === 0 && <p className="text-[13px] text-muted">Nothing detected.</p>}
              <div className="flex flex-wrap gap-1.5">
                {latest.faces.map((f, i) => (
                  <Chip key={`f${i}`} tone={f.driver_name ? "ok" : "bad"}>
                    {f.driver_name ? `${f.driver_name} ${percent(f.similarity ?? 0)}` : "Unknown face"}
                  </Chip>
                ))}
                {latest.plates.map((p, i) => (
                  <Chip key={`p${i}`} tone={p.vehicle_id ? "ok" : "muted"}>{p.text || "plate"}</Chip>
                ))}
              </div>
            </Card>
          )}

          {!live && job.status === "completed" && frames.length === 0 && (
            <Empty title="No detections">The models found no faces or plates in this video.</Empty>
          )}
          <p className="text-[12px] text-muted">Status: <Badge tone={STATUS_TONE[job.status]}>{job.status}</Badge> · attempts {job.attempts}</p>
        </div>
      </div>
    </>
  );
}
