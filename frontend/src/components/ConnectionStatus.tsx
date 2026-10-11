import { useState } from "react";
import { useSession } from "../session";
import { isDesktop, setServerUrl } from "../api/client";
import { formatBytes } from "../lib/format";
import { cn } from "../lib/cn";
import { ServerScanner } from "./ServerScanner";
import { Badge, Button, Modal, Spinner, StatusDot } from "./ui";

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

  const checkNow = async () => {
    setChecking(true);
    await refreshHealth();
    setCheckedAt(new Date());
    setChecking(false);
  };

  const reconnect = async (url: string) => {
    setServerUrl(url);
    await checkNow();
  };

  const { tone, label } = summarize(health, healthError);
  const reachable = !healthError;

  return (
    <Modal title="Device connection" onClose={onClose} footer={
      <Button variant="primary" disabled={checking} onClick={checkNow}>{checking ? <Spinner /> : "Check now"}</Button>
    }>
      <div className="flex flex-col gap-3">
        <div className="flex items-center justify-between">
          <Badge tone={tone}>{label}</Badge>
          {checkedAt && <span className="text-[12px] text-muted">Checked {checkedAt.toLocaleTimeString()}</span>}
        </div>

        {reachable && health && (
          <dl className="facts">
            <dt>Free disk</dt><dd>{formatBytes(health.disk.free_bytes)} of {formatBytes(health.disk.total_bytes)}</dd>
            <dt>Version</dt><dd>{health.version}</dd>
          </dl>
        )}

        {!reachable && (
          <div className="flex flex-col gap-2">
            <p className="text-[13px] text-muted">{healthError?.message ?? "The Guard++ device is not responding."}</p>
            {isDesktop ? (
              <ServerScanner onPick={(u) => void reconnect(u)} />
            ) : (
              <p className="text-[12px] text-muted">Ask an administrator to check the Guard++ service on the device.</p>
            )}
          </div>
        )}
      </div>
    </Modal>
  );
}

/** Topbar indicator: reachability at a glance; opens a panel to re-check or find the server on the LAN. */
export function ConnectionStatus() {
  const { health, healthError } = useSession();
  const [open, setOpen] = useState(false);
  if (!health && !healthError) return null;
  const { tone, label } = summarize(health, healthError);
  return (
    <>
      <button
        type="button"
        onClick={() => setOpen(true)}
        title="Check device connection"
        className={cn(
          "flex items-center gap-2 rounded-md border border-transparent px-2.5 py-1.5 text-[12.5px] text-muted transition-colors hover:bg-surface-2 hover:text-text",
        )}
      >
        <StatusDot tone={tone} />
        <span className="hidden sm:inline">{label}</span>
      </button>
      {open && <ConnectionModal onClose={() => setOpen(false)} />}
    </>
  );
}
