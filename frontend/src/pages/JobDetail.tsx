import { useEffect, useMemo, useState } from "react";
import { Link, useNavigate, useParams } from "react-router-dom";
import { api, subscribe, type Frame, type Job, type VideoInfo } from "../api/client";
import { useSession } from "../session";
import { formatBytes, formatDuration, formatTime, percent, STATUS_TONE, VERDICTS } from "../lib/format";
import { Badge, Empty, ErrorBox, PageHeader, Progress, Spinner } from "../components/ui";
import { isImageName } from "../lib/format";
import { VideoOverlay } from "../components/VideoOverlay";

const FINISHED = ["completed", "failed", "cancelled"];

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
  if (!job) return <Spinner label="Loading job…" />;

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
            {live && <button className="btn" onClick={cancel}>Cancel</button>}
            {!live && user?.role === "admin" && <button className="btn btn-danger" onClick={remove}>Delete</button>}
          </>
        }
      />

      <div className="job-layout">
        <div className="card card-flush">
          <VideoOverlay src={api.videoUrl(id)} image={isImageName(job.original_name)} frames={frames} width={video?.width} height={video?.height} holdMs={sampleGap} />
          <div className="legend small muted">
            <span><i className="sw sw-ok" /> recognized</span>
            <span><i className="sw sw-bad" /> unknown face</span>
            <span><i className="sw sw-plate" /> unregistered plate</span>
          </div>
        </div>

        <div className="stack">
          <div className="card">
            {job.status === "queued" && (
              <>
                <Badge tone="info">Queued</Badge>
                <p className="muted">
                  {job.queue_position ? `${job.queue_position} job(s) ahead. ` : ""}Waiting for the AI engine…
                </p>
              </>
            )}
            {job.status === "running" && (
              <>
                <div className="card-head"><Badge tone="info">Analysing</Badge><span className="muted">{percent(job.progress)}</span></div>
                <Progress value={job.progress} />
                <p className="muted small">{frames.length} frames analysed</p>
              </>
            )}
            {job.status === "failed" && <><Badge tone="bad">Failed</Badge><ErrorBox error={job.error ?? "Analysis failed"} /></>}
            {job.status === "cancelled" && <Badge tone="muted">Cancelled</Badge>}
            {job.status === "completed" && result && verdict && (
              <div className={`verdict verdict-${verdict.tone}`}>
                <div className="verdict-title">{verdict.label}</div>
                <div>{result.reason}</div>
              </div>
            )}
          </div>

          {result && (
            <div className="card">
              <h2>Evidence</h2>
              <dl className="facts">
                <dt>Driver</dt>
                <dd>{result.driver ? <Link to="/drivers">{result.driver.name}</Link> : "—"}
                  {result.driver && <span className="muted small"> · {percent(result.driver.similarity)} match in {result.driver.frames} frame(s)</span>}</dd>
                <dt>Vehicle</dt>
                <dd>{result.vehicle ? <Link to="/vehicles">{result.vehicle.plate_number}</Link> : "—"}
                  {result.vehicle && <span className="muted small"> · read {result.vehicle.reads}×</span>}</dd>
                <dt>Faces</dt>
                <dd>{result.faces_seen} seen, {result.unknown_faces} unknown</dd>
                <dt>Frames</dt>
                <dd>{result.frames_analysed} analysed in {formatDuration(result.processing_ms)}</dd>
              </dl>
              {result.plates_read.length > 0 && (
                <>
                  <h3>Plates read</h3>
                  <div className="chips">
                    {result.plates_read.map((p) => (
                      <span key={p.text} className={`chip ${p.vehicle_id ? "chip-ok" : ""}`}>{p.text} ×{p.reads}</span>
                    ))}
                  </div>
                </>
              )}
              {result.drivers_seen.length > 1 && (
                <>
                  <h3>Drivers seen</h3>
                  <div className="chips">
                    {result.drivers_seen.map((d) => <span key={d.id} className="chip">{d.name} ×{d.frames}</span>)}
                  </div>
                </>
              )}
            </div>
          )}

          {live && latest && (
            <div className="card">
              <h2>Latest frame</h2>
              <p className="muted small">Frame {latest.frame} · {(latest.t_ms / 1000).toFixed(1)} s</p>
              {latest.faces.length === 0 && latest.plates.length === 0 && <p className="muted">Nothing detected.</p>}
              <div className="chips">
                {latest.faces.map((f, i) => (
                  <span key={`f${i}`} className={`chip ${f.driver_name ? "chip-ok" : "chip-bad"}`}>
                    {f.driver_name ? `${f.driver_name} ${percent(f.similarity ?? 0)}` : "Unknown face"}
                  </span>
                ))}
                {latest.plates.map((p, i) => (
                  <span key={`p${i}`} className={`chip ${p.vehicle_id ? "chip-ok" : ""}`}>{p.text || "plate"}</span>
                ))}
              </div>
            </div>
          )}

          {!live && job.status === "completed" && frames.length === 0 && (
            <Empty title="No detections">The models found no faces or plates in this video.</Empty>
          )}
          <p className="small muted">Status: <Badge tone={STATUS_TONE[job.status]}>{job.status}</Badge> · attempts {job.attempts}</p>
        </div>
      </div>
    </>
  );
}
