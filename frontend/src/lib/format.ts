import type { Verdict } from "../api/client";

export function formatTime(iso: string | null | undefined): string {
  if (!iso) return "—";
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return iso;
  return d.toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" });
}

export function formatRelative(iso: string | null | undefined): string {
  if (!iso) return "—";
  const s = Math.round((Date.now() - new Date(iso).getTime()) / 1000);
  if (s < 45) return "just now";
  if (s < 3600) return `${Math.round(s / 60)} min ago`;
  if (s < 86400) return `${Math.round(s / 3600)} h ago`;
  return formatTime(iso);
}

export function formatBytes(n: number): string {
  if (n < 1024) return `${n} B`;
  const units = ["KB", "MB", "GB", "TB"];
  let v = n / 1024;
  let i = 0;
  while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
  return `${v.toFixed(v < 10 ? 1 : 0)} ${units[i]}`;
}

export function formatDuration(ms: number | undefined): string {
  if (!ms && ms !== 0) return "—";
  const s = ms / 1000;
  return s < 60 ? `${s.toFixed(1)} s` : `${Math.floor(s / 60)} min ${Math.round(s % 60)} s`;
}

export const percent = (x: number) => `${Math.round(x * 100)}%`;

export type Tone = "ok" | "bad" | "warn" | "info" | "muted";

export const VERDICTS: Record<Verdict, { label: string; tone: Tone }> = {
  authorized: { label: "Authorized", tone: "ok" },
  unauthorized: { label: "Unauthorized", tone: "bad" },
  unknown_driver: { label: "Unknown driver", tone: "warn" },
  unknown_vehicle: { label: "Unknown vehicle", tone: "warn" },
  unknown_both: { label: "Nothing recognized", tone: "muted" },
};

export const STATUS_TONE: Record<string, Tone> = {
  active: "ok", inactive: "muted", blacklisted: "bad",
  queued: "info", running: "info", completed: "ok", failed: "bad", cancelled: "muted",
  critical: "bad", warning: "warn", info: "info",
};

export const CAMERA_STATE: Record<string, { label: string; tone: Tone }> = {
  live: { label: "Live", tone: "ok" },
  starting: { label: "Starting", tone: "info" },
  connecting: { label: "Connecting", tone: "info" },
  waiting: { label: "Waiting for AI engine", tone: "warn" },
  reconnecting: { label: "Offline", tone: "bad" },
  error: { label: "Error", tone: "bad" },
  disabled: { label: "Disabled", tone: "muted" },
};

export function formatClock(iso: string | null | undefined): string {
  if (!iso) return "—";
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return iso;
  const sameDay = d.toDateString() === new Date().toDateString();
  return sameDay ? d.toLocaleTimeString(undefined, { timeStyle: "medium" })
                 : d.toLocaleString(undefined, { dateStyle: "short", timeStyle: "short" });
}

export const IMAGE_EXT = /\.(jpe?g|png|bmp|webp)$/i;
export const isImageName = (name?: string | null) => !!name && IMAGE_EXT.test(name);
