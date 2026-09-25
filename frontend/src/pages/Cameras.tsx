import { useEffect, useState } from "react";
import { api, subscribe, type Camera, type ProbeResult } from "../api/client";
import { useAsync } from "../lib/useAsync";
import { useSession } from "../session";
import { CAMERA_STATE, formatRelative } from "../lib/format";
import { Badge, Empty, ErrorBox, Field, Modal, PageHeader, Spinner } from "../components/ui";

// ------------------------------------------------------------------ source types

type Kind = "csi" | "usb" | "rtsp" | "http" | "file" | "gst";

const KINDS: Record<Kind, { label: string; field: string; placeholder: string; hint: string }> = {
  csi: { label: "Jetson CSI camera", field: "Sensor number", placeholder: "0",
         hint: "The camera on the Jetson's CSI connector (0 for the first, 1 for the second)." },
  usb: { label: "USB camera", field: "Device number", placeholder: "0", hint: "0 means /dev/video0." },
  rtsp: { label: "IP camera (RTSP)", field: "RTSP address", placeholder: "rtsp://user:password@192.168.1.64:554/stream1",
          hint: "Find the address in the camera's manual or web settings. H.264/H.265 are hardware-decoded." },
  http: { label: "HTTP stream (MJPEG/HLS)", field: "URL", placeholder: "http://192.168.1.70/video.mjpg", hint: "" },
  file: { label: "Video file (testing)", field: "File path on the Jetson", placeholder: "/var/lib/guard/test-videos/gate.mp4",
          hint: "Plays the file in a loop as if it were a camera. For testing only. Copy clips with: sudo cp clip.mp4 /var/lib/guard/test-videos/" },
  gst: { label: "Custom GStreamer pipeline", field: "Pipeline", placeholder: "v4l2src device=/dev/video0 ! videoconvert ! appsink",
         hint: "Advanced. Must end in appsink and produce BGR frames." },
};

function parseSource(source: string): { kind: Kind; value: string; resolution: string } {
  const [path, query = ""] = source.split("?");
  const q = new URLSearchParams(query);
  const res = q.get("width") && q.get("height") ? `${q.get("width")}x${q.get("height")}` : "";
  if (source.startsWith("csi://")) return { kind: "csi", value: path.slice(6), resolution: res };
  if (source.startsWith("usb://")) return { kind: "usb", value: path.slice(6), resolution: res };
  if (source.startsWith("/dev/video")) return { kind: "usb", value: path.slice(10), resolution: res };
  if (/^rtsps?:\/\//.test(source)) return { kind: "rtsp", value: source, resolution: "" };
  if (/^https?:\/\//.test(source)) return { kind: "http", value: source, resolution: "" };
  if (source.startsWith("file://")) return { kind: "file", value: source.slice(7), resolution: "" };
  if (source.startsWith("gst://")) return { kind: "gst", value: source.slice(6), resolution: "" };
  return { kind: "rtsp", value: source, resolution: "" };
}

function buildSource(kind: Kind, value: string, resolution: string): string {
  const v = value.trim();
  const m = /^(\d+)\s*[x×]\s*(\d+)$/.exec(resolution.trim());
  const q = m ? `?width=${m[1]}&height=${m[2]}` : "";
  switch (kind) {
    case "csi": return `csi://${v || "0"}${q}`;
    case "usb": return `usb://${v || "0"}${q}`;
    case "file": return v.startsWith("/") ? `file://${v}` : v;
    case "gst": return `gst://${v}`;
    default: return v;
  }
}

function describeSource(source: string) {
  const { kind, value } = parseSource(source);
  if (kind === "csi") return `CSI camera ${value}`;
  if (kind === "usb") return `USB camera /dev/video${value}`;
  if (kind === "file") return `Test file ${value.split("/").pop()}`;
  if (kind === "gst") return "GStreamer pipeline";
  return source.replace(/\/\/([^:@/]+):([^@]+)@/, "//$1:•••@");   // hide passwords
}

// ------------------------------------------------------------------ page

export function CamerasPage() {
  const { user } = useSession();
  const admin = user?.role === "admin";
  const { data: cameras, error, loading, reload } = useAsync(() => api.cameras(), []);
  const [editing, setEditing] = useState<Camera | "new" | null>(null);

  useEffect(() => subscribe((ev) => { if (ev.type === "camera.status") reload(); }), [reload]);
  useEffect(() => { const t = setInterval(reload, 10000); return () => clearInterval(t); }, [reload]);

  const toggle = async (c: Camera) => {
    try { await api.updateCamera(c.id, { enabled: !c.enabled }); reload(); } catch (e) { alert((e as Error).message); }
  };
  const remove = async (c: Camera) => {
    if (!confirm(`Remove camera "${c.name}"? Its access log entries are kept.`)) return;
    try { await api.deleteCamera(c.id); reload(); } catch (e) { alert((e as Error).message); }
  };

  return (
    <>
      <PageHeader
        title="Cameras"
        subtitle="Every enabled camera is analysed continuously; each vehicle or person passing becomes an access event."
        actions={admin && <button className="btn btn-primary" onClick={() => setEditing("new")}>Add camera</button>}
      />
      <ErrorBox error={error} onRetry={reload} />
      {loading && !cameras ? <Spinner /> : !cameras?.length ? (
        <Empty title="No cameras">{admin ? "Add the first camera to start live monitoring." : "An administrator needs to add cameras."}</Empty>
      ) : (
        <div className="card card-flush table-wrap">
          <table>
            <thead><tr><th>Name</th><th>Source</th><th>Status</th><th>Analysis</th><th>Last frame</th>{admin && <th />}</tr></thead>
            <tbody>
              {cameras.map((c) => {
                const st = CAMERA_STATE[c.status.state] ?? { label: c.status.state, tone: "muted" as const };
                return (
                  <tr key={c.id}>
                    <td><strong>{c.name}</strong></td>
                    <td className="break small">{describeSource(c.source)}</td>
                    <td>
                      <div className="stack-tight">
                        <Badge tone={st.tone}>{st.label}</Badge>
                        {c.status.state !== "live" && c.status.message && <span className="muted small break">{c.status.message}</span>}
                      </div>
                    </td>
                    <td className="nowrap small">
                      {c.status.state === "live"
                        ? <>{c.status.analysis_fps.toFixed(1)} / {c.sample_fps} fps<br /><span className="muted">{c.status.width}×{c.status.height}</span></>
                        : <span className="muted">{c.sample_fps} fps target</span>}
                    </td>
                    <td className="nowrap small">{formatRelative(c.status.last_frame_at)}</td>
                    {admin && (
                      <td className="nowrap">
                        <div className="row">
                          <button className="btn btn-small" onClick={() => toggle(c)}>{c.enabled ? "Disable" : "Enable"}</button>
                          <button className="btn btn-small" onClick={() => setEditing(c)}>Edit</button>
                          <button className="btn btn-small btn-danger" onClick={() => remove(c)}>Remove</button>
                        </div>
                      </td>
                    )}
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      )}
      {editing && (
        <CameraForm camera={editing === "new" ? null : editing} onClose={() => setEditing(null)} onSaved={() => { setEditing(null); reload(); }} />
      )}
    </>
  );
}

function CameraForm({ camera, onClose, onSaved }: { camera: Camera | null; onClose: () => void; onSaved: () => void }) {
  const initial = camera ? parseSource(camera.source) : { kind: "csi" as Kind, value: "0", resolution: "" };
  const [name, setName] = useState(camera?.name ?? "");
  const [kind, setKind] = useState<Kind>(initial.kind);
  const [value, setValue] = useState(initial.value);
  const [resolution, setResolution] = useState(initial.resolution);
  const [fps, setFps] = useState(String(camera?.sample_fps ?? 5));
  const [enabled, setEnabled] = useState(camera?.enabled ?? true);
  const [probe, setProbe] = useState<ProbeResult | null>(null);
  const [probing, setProbing] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const source = buildSource(kind, value, resolution);
  const k = KINDS[kind];
  useEffect(() => setProbe(null), [source]);

  const test = async () => {
    setProbing(true);
    setError(null);
    try { setProbe(await api.probeCamera(source)); }
    catch (e) { setError((e as Error).message); }
    finally { setProbing(false); }
  };

  const save = async () => {
    setSaving(true);
    setError(null);
    const body = { name, source, sample_fps: Number(fps), enabled };
    try {
      if (camera) await api.updateCamera(camera.id, body);
      else await api.createCamera(body);
      onSaved();
    } catch (e) {
      setError((e as Error).message);
      setSaving(false);
    }
  };

  return (
    <Modal
      title={camera ? `Edit ${camera.name}` : "Add camera"}
      onClose={onClose}
      footer={
        <>
          <button className="btn" onClick={test} disabled={probing || !value.trim()}>{probing ? "Testing…" : "Test connection"}</button>
          <span className="spacer" />
          <button className="btn" onClick={onClose}>Cancel</button>
          <button className="btn btn-primary" onClick={save} disabled={saving || !name.trim() || !value.trim()}>
            {saving ? "Saving…" : "Save"}
          </button>
        </>
      }
    >
      <div className="stack">
        <Field label="Name" hint="Shown in the access log and alerts, e.g. “Main gate – entry”.">
          <input value={name} onChange={(e) => setName(e.target.value)} maxLength={80} autoFocus />
        </Field>
        <Field label="Camera type">
          <select value={kind} onChange={(e) => { const nk = e.target.value as Kind; setKind(nk); setValue(nk === "csi" || nk === "usb" ? "0" : ""); setResolution(""); }}>
            {(Object.keys(KINDS) as Kind[]).map((key) => <option key={key} value={key}>{KINDS[key].label}</option>)}
          </select>
        </Field>
        <Field label={k.field} hint={k.hint}>
          <input value={value} onChange={(e) => setValue(e.target.value)} placeholder={k.placeholder} className={kind === "gst" ? "mono" : undefined} />
        </Field>
        {(kind === "csi" || kind === "usb") && (
          <Field label="Resolution (optional)" hint={kind === "csi" ? "Default 1920x1080 at 30 fps." : "Default: the camera's own."}>
            <input value={resolution} onChange={(e) => setResolution(e.target.value)} placeholder="1280x720" />
          </Field>
        )}
        <div className="row">
          <Field label="Frames analysed per second" hint="5 is plenty for a gate. Lower it when running many cameras.">
            <input type="number" min={0.5} max={15} step={0.5} value={fps} onChange={(e) => setFps(e.target.value)} />
          </Field>
        </div>
        <label className="toggle"><input type="checkbox" checked={enabled} onChange={(e) => setEnabled(e.target.checked)} /> Enabled (analysed continuously)</label>
        <div className="muted small">Source: <code className="break">{describeSource(source)}</code></div>
        {probe && (probe.ok
          ? <div className="notice notice-ok">Connected: {probe.width}×{probe.height}{probe.fps ? ` at ${Math.round(probe.fps)} fps` : ""} ({probe.backend}).</div>
          : <div className="error-box">{probe.error}</div>)}
        <ErrorBox error={error} />
      </div>
    </Modal>
  );
}
