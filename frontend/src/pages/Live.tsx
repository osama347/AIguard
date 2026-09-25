import { useCallback, useEffect, useRef, useState } from "react";
import { Link } from "react-router-dom";
import { api, subscribe, type AccessEvent, type Camera, type LiveFrame } from "../api/client";
import { useSession } from "../session";
import { formatClock, formatDuration, formatTime, VERDICTS } from "../lib/format";
import { LiveTile } from "../components/LiveTile";
import { Snapshot } from "../components/Snapshot";
import { Badge, Empty, ErrorBox, Modal, PageHeader, Spinner } from "../components/ui";

const FEED_SIZE = 30;

export function LivePage() {
  const { isAdmin } = useSession();
  const [cameras, setCameras] = useState<Camera[] | null>(null);
  const [error, setError] = useState<Error | null>(null);
  const [feed, setFeed] = useState<AccessEvent[]>([]);
  const [focus, setFocus] = useState<number | null>(null);
  const [selected, setSelected] = useState<AccessEvent | null>(null);
  const frames = useRef(new Map<number, { frame: LiveFrame; at: number }>());

  const loadCameras = useCallback(() => {
    api.cameras().then((c) => { setCameras(c); setError(null); }).catch(setError);
  }, []);

  useEffect(() => {
    loadCameras();
    api.accessEvents(FEED_SIZE, 0, { source: "camera" }).then(setFeed).catch(() => {});
    const t = setInterval(loadCameras, 10000);   // refresh rates and status text
    return () => clearInterval(t);
  }, [loadCameras]);

  useEffect(() => subscribe((ev) => {
    if (ev.type === "camera.frame") {
      // Kept outside React state: tiles redraw from it every animation frame.
      frames.current.set(ev.data.camera_id, { frame: ev.data, at: performance.now() });
    } else if (ev.type === "camera.status") {
      setCameras((cs) => cs?.map((c) => c.id === ev.data.camera_id
        ? { ...c, status: { ...c.status, state: ev.data.state, message: ev.data.message } } : c) ?? null);
    } else if (ev.type === "access.event" && ev.data.source === "camera") {
      setFeed((f) => [ev.data, ...f.filter((e) => e.id !== ev.data.id)].slice(0, FEED_SIZE));
    }
  }), []);

  const enabled = cameras?.filter((c) => c.enabled) ?? [];
  const focused = enabled.find((c) => c.id === focus);
  const live = enabled.filter((c) => c.status.state === "live").length;

  return (
    <>
      <PageHeader
        title="Live"
        subtitle={cameras ? `${live} of ${enabled.length} camera${enabled.length === 1 ? "" : "s"} live · analysed around the clock` : undefined}
        actions={isAdmin ? <Link className="btn" to="/cameras">Manage cameras</Link> : undefined}
      />
      <ErrorBox error={error} onRetry={loadCameras} />
      {!cameras ? <Spinner /> : !enabled.length ? (
        <Empty title="No cameras yet">
          {isAdmin
            ? <>Add the gate cameras on the <Link to="/cameras">Cameras</Link> page; they are analysed continuously from then on.</>
            : "An administrator needs to add cameras."}
        </Empty>
      ) : (
        <div className="live-layout">
          <div className="stack">
            {focused ? (
              <>
                <LiveTile camera={focused} frames={frames} large onClick={() => setFocus(null)} />
                <div className="row">
                  <button className="btn btn-small" onClick={() => setFocus(null)}>← All cameras</button>
                  <span className="muted small">Click the picture to go back.</span>
                </div>
              </>
            ) : (
              <div className={`tile-grid ${enabled.length === 1 ? "tile-grid-one" : ""}`}>
                {enabled.map((c) => (
                  <LiveTile key={c.id} camera={c} frames={frames} onClick={enabled.length > 1 ? () => setFocus(c.id) : undefined} />
                ))}
              </div>
            )}
            <div className="legend muted small">
              <span><span className="sw sw-ok" />Known driver / registered plate</span>
              <span><span className="sw sw-bad" />Unknown face</span>
              <span><span className="sw sw-plate" />Unregistered plate</span>
            </div>
          </div>

          <aside className="card card-flush feed">
            <div className="feed-head">
              <h2>Recent events</h2>
              <Link to="/access" className="small">Access log →</Link>
            </div>
            {!feed.length ? (
              <div className="muted small feed-empty">Vehicles and people passing the cameras will appear here.</div>
            ) : (
              <ul className="feed-list">
                {feed.map((e) => (
                  <li key={e.id}>
                    <button className="feed-item" onClick={() => setSelected(e)}>
                      <Snapshot event={e} className="snapshot-thumb" />
                      <div className="stack-tight feed-text">
                        <Badge tone={VERDICTS[e.verdict]?.tone ?? "muted"}>{VERDICTS[e.verdict]?.label ?? e.verdict}</Badge>
                        <span className="break">
                          {e.driver_name ?? "Unknown driver"}
                          {e.plate_text && <> · <span className="mono">{e.plate_text}</span></>}
                        </span>
                        <span className="muted small">{e.camera_name ?? "Removed camera"} · {formatClock(e.event_time)}</span>
                      </div>
                    </button>
                  </li>
                ))}
              </ul>
            )}
          </aside>
        </div>
      )}
      {selected && <EventModal event={selected} onClose={() => setSelected(null)} />}
    </>
  );
}

export function EventModal({ event: e, onClose }: { event: AccessEvent; onClose: () => void }) {
  const { isAdmin } = useSession();
  const v = VERDICTS[e.verdict];
  const duration = e.details?.duration_ms;
  return (
    <Modal title={v?.label ?? e.verdict} onClose={onClose} wide>
      <div className="event-detail">
        <Snapshot event={e} className="snapshot-large" />
        <div className="stack">
          <div className={`verdict verdict-${v?.tone ?? "muted"}`}>
            <div className="verdict-title">{v?.label ?? e.verdict}</div>
            <div>{e.details?.reason}</div>
          </div>
          <dl className="facts">
            <dt>When</dt><dd>{formatTime(e.event_time)}</dd>
            {duration !== undefined && <><dt>In view</dt><dd>{formatDuration(duration)}</dd></>}
            <dt>Source</dt>
            <dd>{e.source === "camera" ? e.camera_name ?? "Removed camera"
                 : e.job_id && isAdmin ? <Link to={`/jobs/${e.job_id}`} onClick={onClose}>Test video</Link> : "Test video"}</dd>
            <dt>Driver</dt><dd>{e.driver_name ?? "—"}</dd>
            <dt>Plate</dt><dd className="mono">{e.plate_text || "—"}</dd>
            {e.details?.frames !== undefined && <><dt>Frames</dt><dd>{e.details.frames}</dd></>}
          </dl>
        </div>
      </div>
    </Modal>
  );
}
