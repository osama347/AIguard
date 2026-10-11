import { useCallback, useEffect, useRef, useState } from "react";
import { Link } from "react-router-dom";
import { api, subscribe, type AccessEvent, type Camera, type LiveFrame } from "../api/client";
import { useSession } from "../session";
import { cn } from "../lib/cn";
import { formatClock, formatDuration, formatTime, VERDICTS } from "../lib/format";
import { LiveTile } from "../components/LiveTile";
import { Snapshot } from "../components/Snapshot";
import { Badge, buttonClass, Card, Empty, ErrorBox, Fact, Facts, Modal, PageHeader, Spinner } from "../components/ui";

const FEED_SIZE = 30;

const VERDICT_BOX: Record<string, string> = {
  ok: "bg-ok/12 text-ok border-ok/22",
  bad: "bg-bad/12 text-bad border-bad/22",
  warn: "bg-warn/13 text-warn border-warn/22",
  info: "bg-info/12 text-info border-info/22",
  muted: "bg-surface-2 text-muted border-line",
};

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
        actions={isAdmin ? <Link className={buttonClass()} to="/cameras">Manage cameras</Link> : undefined}
      />
      <ErrorBox error={error} onRetry={loadCameras} />
      {!cameras ? <div className="grid place-items-center py-16"><Spinner /></div> : !enabled.length ? (
        <Empty title="No cameras yet">
          {isAdmin
            ? <>Add the gate cameras on the <Link to="/cameras" className="text-accent">Cameras</Link> page; they are analysed continuously from then on.</>
            : "An administrator needs to add cameras."}
        </Empty>
      ) : (
        <div className="grid items-start gap-4 lg:grid-cols-[minmax(0,1fr)_340px]">
          <div className="flex min-w-0 flex-col gap-3.5">
            {focused ? (
              <>
                <LiveTile camera={focused} frames={frames} large onClick={() => setFocus(null)} />
                <div className="flex flex-wrap items-center gap-3">
                  <button className={buttonClass("secondary", "sm")} onClick={() => setFocus(null)}>← All cameras</button>
                  <span className="text-[12px] text-muted">Click the picture to go back.</span>
                </div>
              </>
            ) : (
              <div className={cn("grid gap-3", enabled.length === 1 ? "grid-cols-1" : "grid-cols-[repeat(auto-fill,minmax(360px,1fr))]")}>
                {enabled.map((c) => (
                  <LiveTile key={c.id} camera={c} frames={frames} onClick={enabled.length > 1 ? () => setFocus(c.id) : undefined} />
                ))}
              </div>
            )}
            <div className="flex flex-wrap gap-4 text-[12px] text-muted">
              <span className="inline-flex items-center gap-1.5"><span className="size-2.5 rounded-[3px] bg-ok" />Known driver / registered plate</span>
              <span className="inline-flex items-center gap-1.5"><span className="size-2.5 rounded-[3px] bg-bad" />Unknown face</span>
              <span className="inline-flex items-center gap-1.5"><span className="size-2.5 rounded-[3px] bg-plate" />Unregistered plate</span>
            </div>
          </div>

          <Card flush className="lg:sticky lg:top-[68px] lg:mb-0">
            <div className="flex items-baseline justify-between px-4 pb-2 pt-3.5">
              <h2 className="text-sm font-semibold">Recent events</h2>
              <Link to="/access" className="text-[12px] text-muted hover:text-text">Access log →</Link>
            </div>
            {!feed.length ? (
              <div className="px-4 pb-4 text-[13px] text-muted">Vehicles and people passing the cameras will appear here.</div>
            ) : (
              <ul className="m-0 max-h-[calc(100vh-160px)] list-none overflow-y-auto p-2 pt-0">
                {feed.map((e) => (
                  <li key={e.id}>
                    <button className="flex w-full items-start gap-2.5 rounded-lg p-2 text-left transition-colors hover:bg-surface-2" onClick={() => setSelected(e)}>
                      <Snapshot event={e} className="snapshot-thumb" />
                      <div className="flex min-w-0 flex-col gap-1">
                        <Badge tone={VERDICTS[e.verdict]?.tone ?? "muted"}>{VERDICTS[e.verdict]?.label ?? e.verdict}</Badge>
                        <span className="break-words text-[13px]">
                          {e.driver_name ?? "Unknown driver"}
                          {e.plate_text && <> · <span className="font-mono">{e.plate_text}</span></>}
                        </span>
                        <span className="text-[12px] text-muted">{e.camera_name ?? "Removed camera"} · {formatClock(e.event_time)}</span>
                      </div>
                    </button>
                  </li>
                ))}
              </ul>
            )}
          </Card>
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
      <div className="grid items-start gap-5 md:grid-cols-[minmax(0,1.6fr)_minmax(220px,1fr)]">
        <Snapshot event={e} className="snapshot-large" />
        <div className="flex flex-col gap-3">
          <div className={cn("rounded-lg border px-4 py-3.5", VERDICT_BOX[v?.tone ?? "muted"])}>
            <div className="mb-1 text-[18px] font-semibold">{v?.label ?? e.verdict}</div>
            <div className="text-text">{e.details?.reason}</div>
          </div>
          <Facts>
            <Fact label="When">{formatTime(e.event_time)}</Fact>
            {duration !== undefined && <Fact label="In view">{formatDuration(duration)}</Fact>}
            <Fact label="Source">
              {e.source === "camera" ? e.camera_name ?? "Removed camera"
               : e.job_id && isAdmin ? <Link to={`/jobs/${e.job_id}`} onClick={onClose}>Test video</Link> : "Test video"}
            </Fact>
            <Fact label="Driver">{e.driver_name ?? "—"}</Fact>
            <Fact label="Plate"><span className="font-mono">{e.plate_text || "—"}</span></Fact>
            {e.details?.frames !== undefined && <Fact label="Frames">{e.details.frames}</Fact>}
          </Facts>
        </div>
      </div>
    </Modal>
  );
}
