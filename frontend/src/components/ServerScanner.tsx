import { useEffect, useState } from "react";
import { discoverServers, type FoundServer } from "../api/client";
import { Button, Spinner } from "./ui";

/** mDNS scan for Guard++ servers on the LAN, so reconnecting never requires typing an
 *  address — shared by the first-connect screen, the connection-status panel, and
 *  Settings. With `autoScan`, starts scanning immediately (first-connect); otherwise
 *  shows a button and waits to be asked (reconnecting an already-working session). */
export function ServerScanner({ onPick, autoScan = false, busy, initialLabel = "Search for it on this network" }: {
  onPick: (url: string) => void; autoScan?: boolean; busy?: boolean; initialLabel?: string;
}) {
  const [found, setFound] = useState<FoundServer[] | null>(null);
  const [scanning, setScanning] = useState(false);

  const scan = async () => {
    setScanning(true);
    try { setFound(await discoverServers()); } catch { setFound([]); } finally { setScanning(false); }
  };
  useEffect(() => { if (autoScan) void scan(); }, []);

  if (found === null && !scanning) {
    return <Button disabled={busy} onClick={() => void scan()}>{initialLabel}</Button>;
  }

  return (
    <div className="flex flex-col gap-2">
      {scanning && <Spinner label="Searching this network…" />}
      {!scanning && found?.length === 0 && <p className="text-[13px] text-muted">Nothing found on this network.</p>}
      {found?.map((d) => (
        <Button key={d.url} className="justify-start" disabled={busy} onClick={() => onPick(d.url)}>{d.name} — {d.url}</Button>
      ))}
      {!scanning && <button type="button" className="btn-link self-start text-[13px]" onClick={() => void scan()}>Search again</button>}
    </div>
  );
}
