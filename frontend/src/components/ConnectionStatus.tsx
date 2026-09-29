import { useState } from "react";
import { useSession } from "../session";
import { discoverServers, isDesktop, setServerUrl, type FoundServer } from "../api/client";
import { formatBytes } from "../lib/format";
import { Badge, Modal, Spinner } from "./ui";

/** Label/tone for the topbar indicator, shared with the modal's status line. */
function summarize(health: ReturnType<typeof useSession>["health"], healthError: Error | null) {
  if (healthError) return { tone: "bad" as const, label: "Service offline" };
  if (!health) return { tone: "muted" as const, label: "Checking…" };
  if (health.inference.status === "ok") return { tone: "ok" as const, label: "AI engine ready" };
  if (health.inference.status === "loading") return { tone: "warn" as const, label: "AI engine preparing…" };
  return { tone: "bad" as const, label: `AI engine ${health.inference.status}` };
}

function ConnectionModal({ onClose }: { onClose: () => void }) {
  const { health, healthError, refreshHealth } = useSession();
  const [checking, setChecking] = useState(false);
  const [checkedAt, setCheckedAt] = useState<Date | null>(null);
  const [scanning, setScanning] = useState(false);
  const [found, setFound] = useState<FoundServer[] | null>(null);

  const checkNow = async () => {
    setChecking(true);
    await refreshHealth();
    setCheckedAt(new Date());
    setChecking(false);
  };

  const scan = async () => {
    setScanning(true);
    try { setFound(await discoverServers()); } catch { setFound([]); } finally { setScanning(false); }
  };

  const reconnect = async (url: string) => {
    setServerUrl(url);
    await checkNow();
  };

  const { tone, label } = summarize(health, healthError);
  const reachable = !healthError;

  return (
    <Modal title="Device connection" onClose={onClose} footer={
      <button className="btn btn-primary" disabled={checking} onClick={checkNow}>{checking ? <Spinner /> : "Check now"}</button>
    }>
      <div className="stack">
        <div className="row" style={{ alignItems: "center", justifyContent: "space-between" }}>
          <Badge tone={tone}>{label}</Badge>
          {checkedAt && <span className="muted small">Checked {checkedAt.toLocaleTimeString()}</span>}
        </div>

        {reachable && health && (
          <dl className="facts">
            <dt>Free disk</dt><dd>{formatBytes(health.disk.free_bytes)} of {formatBytes(health.disk.total_bytes)}</dd>
            <dt>Version</dt><dd>{health.version}</dd>
          </dl>
        )}

        {!reachable && (
          <div className="stack">
            <p className="muted">{healthError?.message ?? "The Guard++ device is not responding."}</p>
            {isDesktop ? (
              <div className="stack">
                <button className="btn" disabled={scanning} onClick={scan}>
                  {scanning ? <Spinner label="Searching this network…" /> : "Search for it on this network"}
                </button>
                {found && found.length === 0 && <p className="muted small">Nothing found. Ask an administrator to check the device.</p>}
                {found?.map((d) => (
                  <button key={d.url} className="btn btn-small" onClick={() => void reconnect(d.url)}>{d.name} — {d.url}</button>
                ))}
              </div>
            ) : (
              <p className="muted small">Ask an administrator to check the Guard++ service on the device.</p>
            )}
          </div>
        )}
      </div>
    </Modal>
  );
}

/** Topbar indicator, clickable by admins and guards alike: shows reachability at a glance and,
 *  on tap, lets either of them re-check or find the server on the LAN without knowing its address. */
export function ConnectionStatus() {
  const { health, healthError } = useSession();
  const [open, setOpen] = useState(false);
  if (!health && !healthError) return null;
  const { tone, label } = summarize(health, healthError);
  return (
    <>
      <button type="button" className="badge-btn" onClick={() => setOpen(true)} title="Check device connection">
        <Badge tone={tone}>{label}</Badge>
      </button>
      {open && <ConnectionModal onClose={() => setOpen(false)} />}
    </>
  );
}
