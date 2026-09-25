import { useEffect, useRef, useState, type DragEvent } from "react";
import { Link, useNavigate } from "react-router-dom";
import { api, uploadVideo, type Job } from "../api/client";
import { useSession } from "../session";
import { formatBytes, isImageName, formatRelative, percent, STATUS_TONE, VERDICTS } from "../lib/format";
import { Badge, ErrorBox, PageHeader, Progress } from "../components/ui";

export function AnalyzePage() {
  const navigate = useNavigate();
  const { health } = useSession();
  const [file, setFile] = useState<File | null>(null);
  const [preview, setPreview] = useState<string | null>(null);
  const [progress, setProgress] = useState<number | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [dragging, setDragging] = useState(false);
  const [recent, setRecent] = useState<Job[]>([]);
  const input = useRef<HTMLInputElement>(null);

  useEffect(() => { api.jobs(6).then(setRecent).catch(() => {}); }, []);
  useEffect(() => () => { if (preview) URL.revokeObjectURL(preview); }, [preview]);

  const choose = (f: File | undefined) => {
    if (!f) return;
    if (!f.type.startsWith("video/") && !f.type.startsWith("image/") &&
        !/\.(mp4|mov|mkv|avi|webm|m4v)$/i.test(f.name) && !isImageName(f.name)) {
      setError("Please choose a video or picture file.");
      return;
    }
    setError(null);
    setFile(f);
    setPreview(URL.createObjectURL(f));
  };

  const onDrop = (e: DragEvent) => {
    e.preventDefault();
    setDragging(false);
    choose(e.dataTransfer.files[0]);
  };

  const start = async () => {
    if (!file) return;
    setError(null);
    setProgress(0);
    try {
      const job = await uploadVideo(file, setProgress);
      navigate(`/jobs/${job.id}`);
    } catch (e) {
      setError((e as Error).message);
      setProgress(null);
    }
  };

  const engineNote = health && health.inference.status !== "ok"
    ? health.inference.status === "loading"
      ? "The AI engine is still preparing its models. Your upload will be queued and analysed as soon as it is ready."
      : "The AI engine is not available. Uploads will be queued until it is back."
    : null;

  return (
    <>
      <PageHeader title="Test with a video or picture" subtitle="For testing: upload a recorded clip or a still picture and it goes through exactly the same models and rules as the live cameras. Day-to-day monitoring happens on the Live page." />
      {engineNote && <div className="notice">{engineNote}</div>}

      <div className="card">
        {!file ? (
          <div
            className={`dropzone ${dragging ? "dragging" : ""}`}
            onDragOver={(e) => { e.preventDefault(); setDragging(true); }}
            onDragLeave={() => setDragging(false)}
            onDrop={onDrop}
            onClick={() => input.current?.click()}
            role="button"
            tabIndex={0}
            onKeyDown={(e) => e.key === "Enter" && input.current?.click()}
          >
            <strong>Drop a video or picture here</strong>
            <span className="muted">or click to choose a file (MP4, MOV, MKV, AVI, WebM, JPG, PNG, BMP, WebP)</span>
          </div>
        ) : (
          <div className="upload-preview">
            {preview && (isImageName(file.name) || file.type.startsWith("image/")
              ? <img src={preview} alt="Selected picture" className="preview-video" />
              : <video src={preview} controls muted playsInline className="preview-video" />)}
            <div className="stack">
              <div>
                <strong className="break">{file.name}</strong>
                <div className="muted">{formatBytes(file.size)}</div>
              </div>
              {progress !== null ? (
                <>
                  <Progress value={progress} />
                  <span className="muted">Uploading… {percent(progress)}</span>
                </>
              ) : (
                <div className="row">
                  <button className="btn btn-primary" onClick={start}>Run analysis</button>
                  <button className="btn" onClick={() => { setFile(null); setPreview(null); }}>Choose another</button>
                </div>
              )}
            </div>
          </div>
        )}
        <input ref={input} type="file" accept="video/*,image/*" hidden onChange={(e) => choose(e.target.files?.[0])} />
        <ErrorBox error={error} />
      </div>

      {recent.length > 0 && (
        <div className="card">
          <div className="card-head">
            <h2>Recent jobs</h2>
            <Link to="/jobs">All test results</Link>
          </div>
          <ul className="list">
            {recent.map((j) => (
              <li key={j.id}>
                <Link to={`/jobs/${j.id}`} className="list-row">
                  <span className="break">{j.original_name || j.id}</span>
                  <span className="row">
                    {j.verdict ? <Badge tone={VERDICTS[j.verdict].tone}>{VERDICTS[j.verdict].label}</Badge>
                               : <Badge tone={STATUS_TONE[j.status]}>{j.status}</Badge>}
                    <span className="muted small">{formatRelative(j.created_at)}</span>
                  </span>
                </Link>
              </li>
            ))}
          </ul>
        </div>
      )}
    </>
  );
}
