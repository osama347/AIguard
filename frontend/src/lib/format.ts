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

// Pragmatic check, matching the server's: optional leading '+', digits/spaces/dashes,
// 7-15 digits overall. Not full E.164 validation, just enough to catch typos early.
const PHONE_SHAPE = /^\+?[\d\s-]+$/;
export function isValidPhone(phone: string): boolean {
  const p = phone.trim();
  if (!PHONE_SHAPE.test(p)) return false;
  const digits = p.replace(/[^\d]/g, "").length;
  return digits >= 7 && digits <= 15;
}

/** wa.me deep link that opens a chat pre-filled with the code — no WhatsApp API integration needed. */
export function whatsAppShareUrl(phone: string, text: string): string {
  const digits = phone.replace(/[^\d]/g, "");
  return `https://wa.me/${digits}?text=${encodeURIComponent(text)}`;
}

/** The message body sent when sharing a vehicle's authorization code over WhatsApp:
 *  plain text (wa.me URL-encodes it), the code kept prominent on its own line so it
 *  is easy to spot and copy, with a short "what do I do with this" line for the person
 *  who receives it (the owner, who forwards it to drivers they trust). */
export function accessCodeMessage(plate: string, code: string, community?: string): string {
  const who = community?.trim() ? community.trim() : "Guard++";
  return [
    `Your ${who} access code for ${plate}:`,
    "",
    code,
    "",
    "Give this code only to the drivers you authorize — each driver redeems it once, in Guard++, to unlock this vehicle.",
  ].join("\n");
}

/** The message sent to a vehicle's owner asking them to approve a new driver remotely:
 *  the link opens a one-tap Approve/Deny page (see confirmPage in the server), good for
 *  as long as the request hasn't expired. */
export function authorizationRequestMessage(plate: string, driverName: string, link: string, community?: string): string {
  const who = community?.trim() ? community.trim() : "Guard++";
  return [
    `${who}: a request to authorize ${driverName} to drive ${plate} needs your approval.`,
    "",
    link,
    "",
    "Tap the link, then Approve or Deny. This link is single-use and expires after a few days.",
  ].join("\n");
}

export const AUTHZ_STATUS: Record<string, { label: string; tone: Tone }> = {
  pending: { label: "Pending", tone: "info" },
  approved: { label: "Approved", tone: "ok" },
  denied: { label: "Denied", tone: "bad" },
  cancelled: { label: "Cancelled", tone: "muted" },
  expired: { label: "Expired", tone: "warn" },
};

/** The Clipboard API needs a secure context (HTTPS or localhost); this app is typically
 *  served over plain HTTP on the LAN, where navigator.clipboard is undefined. Falls back
 *  to the classic textarea-select-execCommand trick, which works on plain HTTP too. */
export async function copyToClipboard(text: string): Promise<boolean> {
  if (navigator.clipboard?.writeText) {
    try { await navigator.clipboard.writeText(text); return true; } catch { /* fall through */ }
  }
  const el = document.createElement("textarea");
  el.value = text;
  el.style.position = "fixed";
  el.style.opacity = "0";
  document.body.appendChild(el);
  el.focus();
  el.select();
  let ok = false;
  try { ok = document.execCommand("copy"); } catch { ok = false; }
  document.body.removeChild(el);
  return ok;
}
