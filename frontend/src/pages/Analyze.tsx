import { useEffect, useRef, useState, type DragEvent } from "react";
import { Link, useNavigate } from "react-router-dom";
import { UploadSimple } from "@phosphor-icons/react";
import { api, uploadVideo, type Job } from "../api/client";
import { useSession } from "../session";
import { formatBytes, isImageName, formatRelative, percent, STATUS_TONE, VERDICTS } from "../lib/format";
import { cn } from "../lib/cn";
import { Badge, Button, Card, CardHead, ErrorBox, Notice, PageHeader, Progress } from "../components/ui";

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
      {engineNote && <Notice>{engineNote}</Notice>}

      <Card>
        {!file ? (
          <div
            className={cn(
              "flex cursor-pointer flex-col items-center gap-1.5 rounded-lg border-[1.5px] border-dashed border-line-strong px-5 py-12 text-center transition-colors",
              "hover:border-accent hover:bg-accent/5",
              dragging && "border-accent bg-accent/5",
            )}
            onDragOver={(e) => { e.preventDefault(); setDragging(true); }}
            onDragLeave={() => setDragging(false)}
            onDrop={onDrop}
            onClick={() => input.current?.click()}
            role="button"
            tabIndex={0}
            onKeyDown={(e) => e.key === "Enter" && input.current?.click()}
          >
            <UploadSimple size={26} className="text-faint" />
            <strong className="font-medium">Drop a video or picture here</strong>
            <span className="text-[13px] text-muted">or click to choose a file (MP4, MOV, MKV, AVI, WebM, JPG, PNG, BMP, WebP)</span>
          </div>
        ) : (
          <div className="grid items-start gap-5 md:grid-cols-[minmax(0,2fr)_minmax(0,1fr)]">
            {preview && (isImageName(file.name) || file.type.startsWith("image/")
              ? <img src={preview} alt="Selected picture" className="w-full rounded-lg bg-black object-contain" style={{ maxHeight: 360 }} />
              : <video src={preview} controls muted playsInline className="w-full rounded-lg bg-black" style={{ maxHeight: 360 }} />)}
            <div className="flex flex-col gap-3">
              <div>
                <strong className="break-words font-medium">{file.name}</strong>
                <div className="text-[13px] text-muted">{formatBytes(file.size)}</div>
              </div>
              {progress !== null ? (
                <>
                  <Progress value={progress} />
                  <span className="text-[13px] text-muted">Uploading… {percent(progress)}</span>
                </>
              ) : (
                <div className="flex flex-wrap gap-2">
                  <Button variant="primary" onClick={start}>Run analysis</Button>
                  <Button onClick={() => { setFile(null); setPreview(null); }}>Choose another</Button>
                </div>
              )}
            </div>
          </div>
        )}
        <input ref={input} type="file" accept="video/*,image/*" hidden onChange={(e) => choose(e.target.files?.[0])} />
        <ErrorBox error={error} />
      </Card>

      {recent.length > 0 && (
        <Card>
          <CardHead title="Recent jobs" actions={<Link to="/jobs" className="text-[12px] text-muted hover:text-text">All test results →</Link>} />
          <ul className="m-0 list-none p-0">
            {recent.map((j) => (
              <li key={j.id}>
                <Link to={`/jobs/${j.id}`} className="flex items-center justify-between gap-3 border-b border-line py-2.5 last:border-b-0 hover:no-underline">
                  <span className="min-w-0 flex-1 break-words text-[13px]">{j.original_name || j.id}</span>
                  <span className="flex shrink-0 items-center gap-2.5">
                    {j.verdict ? <Badge tone={VERDICTS[j.verdict].tone}>{VERDICTS[j.verdict].label}</Badge>
                               : <Badge tone={STATUS_TONE[j.status]}>{j.status}</Badge>}
                    <span className="text-[12px] text-muted">{formatRelative(j.created_at)}</span>
                  </span>
                </Link>
              </li>
            ))}
          </ul>
        </Card>
      )}
    </>
  );
}
