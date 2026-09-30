// Typed client for the guard-core API (types generated from api/openapi.yaml).
import type { components } from "./schema";

export type Schemas = components["schemas"];
export type Driver = Schemas["Driver"];
export type DriverInput = Schemas["DriverInput"];
export type Vehicle = Schemas["Vehicle"];
export type VehicleInput = Schemas["VehicleInput"];
export type OwnerInput = Schemas["OwnerInput"];
/** Registering a new vehicle always requires its owner, atomically (see api.createVehicle). */
export type NewVehicleInput = VehicleInput & { owner: OwnerInput };
export type Job = Schemas["Job"];
export type JobResult = Schemas["JobResult"];
export type Frame = Schemas["Frame"];
export type FramesDocument = Schemas["FramesDocument"];
export type VideoInfo = Schemas["VideoInfo"];
export type AccessEvent = Schemas["AccessEvent"];
export type Alert = Schemas["Alert"];
export type Health = Schemas["Health"];
export type SystemInfo = Schemas["SystemInfo"];
export type User = Schemas["User"];
export type Role = Schemas["Role"];
export type UserInput = Schemas["UserInput"];
export type LoginResult = Schemas["LoginResult"];
export type EnrollResult = Schemas["EnrollResult"];
export type Verdict = Schemas["Verdict"];
export type Status = Schemas["Status"];
export type Camera = Schemas["Camera"];
export type CameraInput = Schemas["CameraInput"];
export type CameraStatus = Schemas["CameraStatus"];
export type ProbeResult = Schemas["ProbeResult"];
export type LiveFrame = Schemas["LiveFrame"];
export type Overlay = Schemas["Overlay"];
export type FaceDetection = Schemas["FaceDetection"];
export type PlateDetection = Schemas["PlateDetection"];

/** The community this server serves (name, contact details, logo); public so the login screen can show it. */
export interface Community {
  configured: boolean;
  name: string;
  address: string;
  city: string;
  country: string;
  helpline: string;
  email: string;
  website: string;
  logo_url: string | null;
}
export type CommunityInput = Partial<Omit<Community, "configured" | "logo_url">>;

export type Dashboard = Schemas["Dashboard"];
export type VerdictCounts = Schemas["VerdictCounts"];

export interface AccessEventFilter {
  source?: "camera" | "video";
  camera_id?: number;
  verdict?: Verdict;
}

export class ApiError extends Error {
  constructor(public status: number, public code: string, message: string) {
    super(message);
  }
}

// ------------------------------------------------------------------ settings

const SERVER_KEY = "guard.server";
const TOKEN_KEY = "guard.token";

function storageGet(key: string): string | null {
  try { return localStorage.getItem(key); } catch { return null; }
}
function storageSet(key: string, value: string | null) {
  try {
    if (value === null) localStorage.removeItem(key);
    else localStorage.setItem(key, value);
  } catch { /* storage unavailable: settings last for this session only */ }
}

/** True when running inside the desktop (Tauri) shell rather than a browser tab served by guard-core. */
export const isDesktop =
  !/^https?:$/.test(window.location.protocol) || window.location.hostname === "tauri.localhost";

/** Base URL of guard-core. Browser: same origin. Desktop: the Jetson the user connected to ("" until chosen). */
export function serverUrl(): string {
  const saved = storageGet(SERVER_KEY);
  return saved ? saved.replace(/\/+$/, "") : "";
}

export function setServerUrl(url: string | null) {
  storageSet(SERVER_KEY, url ? url.trim().replace(/\/+$/, "") : null);
}

let token: string | null = storageGet(TOKEN_KEY);
const authListeners = new Set<() => void>();

export function getToken() { return token; }
export function setToken(t: string | null) {
  token = t;
  storageSet(TOKEN_KEY, t);
  authListeners.forEach((fn) => fn());
}
export function onAuthChange(fn: () => void) {
  authListeners.add(fn);
  return () => { authListeners.delete(fn); };
}

// ------------------------------------------------------------------ transport

const base = () => {
  if (isDesktop && !serverUrl()) throw new ApiError(0, "network", "No Guard++ server selected yet");
  return serverUrl() + "/api/v1";
};

// A wrong or unreachable server address otherwise hangs on the OS-level TCP
// timeout (can be a minute or more) with no feedback. Uploads (FormData) get
// longer to allow for large files on a slow LAN.
const DEFAULT_TIMEOUT_MS = 10_000;
const UPLOAD_TIMEOUT_MS = 60_000;

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const headers: Record<string, string> = {};
  if (token) headers.Authorization = `Bearer ${token}`;
  let payload: BodyInit | undefined;
  if (body instanceof FormData) payload = body;
  else if (body !== undefined) {
    headers["Content-Type"] = "application/json";
    payload = JSON.stringify(body);
  }

  const controller = new AbortController();
  const timeoutMs = body instanceof FormData ? UPLOAD_TIMEOUT_MS : DEFAULT_TIMEOUT_MS;
  const timer = setTimeout(() => controller.abort(), timeoutMs);

  let res: Response;
  try {
    res = await fetch(base() + path, { method, headers, body: payload, signal: controller.signal });
  } catch (e) {
    if (e instanceof DOMException && e.name === "AbortError") {
      throw new ApiError(0, "timeout", `Timed out reaching Guard++ at ${serverUrl() || window.location.origin}. Check the address and try again.`);
    }
    throw new ApiError(0, "network", `Cannot reach the Guard++ service at ${serverUrl() || window.location.origin}`);
  } finally {
    clearTimeout(timer);
  }
  if (res.status === 204) return undefined as T;
  const data = await res.json().catch(() => null);
  if (!res.ok) {
    if (res.status === 401 && token) setToken(null);
    throw new ApiError(res.status, data?.error ?? "http_error", data?.message ?? `HTTP ${res.status}`);
  }
  return data as T;
}

const get = <T>(p: string) => request<T>("GET", p);
const post = <T>(p: string, b?: unknown) => request<T>("POST", p, b);
const put = <T>(p: string, b?: unknown) => request<T>("PUT", p, b);
const del = (p: string) => request<void>("DELETE", p);

// ------------------------------------------------------------------ endpoints

export const api = {
  health: () => get<Health>("/health"),
  system: () => get<SystemInfo>("/system"),

  setup: (username: string, password: string, extra: { setup_code?: string; community?: CommunityInput } = {}) =>
    post<LoginResult>("/setup", { username, password, ...extra }),
  community: () => get<Community>("/community"),
  saveCommunity: (c: CommunityInput) => put<Community>("/community", c),
  uploadLogo: (file: File) => {
    const fd = new FormData();
    fd.append("logo", file, file.name);
    return put<Community>("/community/logo", fd);
  },
  deleteLogo: () => request<Community>("DELETE", "/community/logo"),
  login: (username: string, password: string) => post<LoginResult>("/auth/login", { username, password }),
  logout: () => post<void>("/auth/logout"),
  me: () => get<User>("/auth/me"),
  changePassword: (current_password: string, new_password: string) =>
    post<void>("/auth/password", { current_password, new_password }),

  dashboard: () => get<Dashboard>("/dashboard"),

  users: () => get<Schemas["UserList"]>("/users").then((r) => r.items),
  createUser: (u: UserInput) => post<User>("/users", u),
  updateUser: (id: number, u: UserInput) => put<User>(`/users/${id}`, u),
  deleteUser: (id: number) => del(`/users/${id}`),

  drivers: () => get<Schemas["DriverList"]>("/drivers").then((r) => r.items),
  driver: (id: number) => get<Driver>(`/drivers/${id}`),
  createDriver: (d: DriverInput) => post<Driver>("/drivers", d),
  updateDriver: (id: number, d: DriverInput) => put<Driver>(`/drivers/${id}`, d),
  deleteDriver: (id: number) => del(`/drivers/${id}`),
  enrollPhotos: (id: number, files: File[]) => {
    const fd = new FormData();
    files.forEach((f) => fd.append("photos", f, f.name));
    return post<EnrollResult>(`/drivers/${id}/photos`, fd);
  },
  clearPhotos: (id: number) => del(`/drivers/${id}/photos`),
  /** Profile picture (display only, admin-only for now) — separate from face-enrollment photos above. */
  setDriverPhoto: (id: number, file: File) => {
    const fd = new FormData();
    fd.append("photo", file, file.name);
    return put<Driver>(`/drivers/${id}/photo`, fd);
  },
  deleteDriverPhoto: (id: number) => request<Driver>("DELETE", `/drivers/${id}/photo`),
  assign: (driverId: number, vehicleId: number) => put<Driver>(`/drivers/${driverId}/vehicles/${vehicleId}`),
  unassign: (driverId: number, vehicleId: number) => del(`/drivers/${driverId}/vehicles/${vehicleId}`),
  /** Redeems an owner's authorization code to add a new authorized driver to their vehicle. */
  authorizeDriver: (code: string, d: DriverInput) => post<Driver>("/drivers/authorize", { code, ...d }),

  vehicles: () => get<Schemas["VehicleList"]>("/vehicles").then((r) => r.items),
  /** Registers a vehicle together with its owner, atomically; the owner is auto-enrolled and authorized. */
  createVehicle: (v: NewVehicleInput) => post<Vehicle>("/vehicles", v),
  updateVehicle: (id: number, v: VehicleInput) => put<Vehicle>(`/vehicles/${id}`, v),
  deleteVehicle: (id: number) => del(`/vehicles/${id}`),
  /** Backfills an owner onto a vehicle that doesn't have one yet (legacy, pre-migration data). */
  setVehicleOwner: (vehicleId: number, owner: OwnerInput) => put<Vehicle>(`/vehicles/${vehicleId}/owner`, owner),

  jobs: (limit = 50, offset = 0) => get<Schemas["JobList"]>(`/jobs?limit=${limit}&offset=${offset}`).then((r) => r.items),
  job: (id: string) => get<Job>(`/jobs/${id}`),
  cancelJob: (id: string) => post<Job>(`/jobs/${id}/cancel`),
  deleteJob: (id: string) => del(`/jobs/${id}`),
  frames: (id: string) => get<FramesDocument>(`/jobs/${id}/frames`),
  videoUrl: (id: string) => `${base()}/jobs/${id}/video?access_token=${encodeURIComponent(token ?? "")}`,

  cameras: () => get<Schemas["CameraList"]>("/cameras").then((r) => r.items),
  createCamera: (c: CameraInput) => post<Camera>("/cameras", c),
  updateCamera: (id: number, c: CameraInput) => put<Camera>(`/cameras/${id}`, c),
  deleteCamera: (id: number) => del(`/cameras/${id}`),
  probeCamera: (source: string) => post<ProbeResult>("/cameras/probe", { source }),
  liveUrl: (id: number, nonce = 0) =>
    `${base()}/cameras/${id}/live.mjpeg?access_token=${encodeURIComponent(token ?? "")}&n=${nonce}`,

  accessEvents: (limit = 100, offset = 0, filter: AccessEventFilter = {}) => {
    const q = new URLSearchParams({ limit: String(limit), offset: String(offset) });
    if (filter.source) q.set("source", filter.source);
    if (filter.camera_id !== undefined) q.set("camera_id", String(filter.camera_id));
    if (filter.verdict) q.set("verdict", filter.verdict);
    return get<Schemas["AccessEventList"]>(`/access-events?${q}`).then((r) => r.items);
  },
  snapshotUrl: (eventId: number) =>
    `${base()}/access-events/${eventId}/snapshot.jpg?access_token=${encodeURIComponent(token ?? "")}`,
  alerts: (openOnly = false) => get<Schemas["AlertList"]>(`/alerts?open=${openOnly}`).then((r) => r.items),
  acknowledgeAlert: (id: number) => post<Alert>(`/alerts/${id}/acknowledge`),
  resolveAlert: (id: number) => post<Alert>(`/alerts/${id}/resolve`),
};

/** Upload a video as a new job, reporting progress (0..1). */
export function uploadVideo(file: File, onProgress: (p: number) => void): Promise<Job> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open("POST", base() + "/jobs");
    if (token) xhr.setRequestHeader("Authorization", `Bearer ${token}`);
    xhr.upload.onprogress = (e) => e.lengthComputable && onProgress(e.loaded / e.total);
    xhr.onload = () => {
      let data: any = null;
      try { data = JSON.parse(xhr.responseText); } catch { /* not JSON */ }
      if (xhr.status === 202) resolve(data as Job);
      else reject(new ApiError(xhr.status, data?.error ?? "http_error", data?.message ?? `HTTP ${xhr.status}`));
    };
    xhr.onerror = () => reject(new ApiError(0, "network", "Upload failed: connection lost"));
    const fd = new FormData();
    fd.append("video", file, file.name);
    xhr.send(fd);
  });
}

// ------------------------------------------------------------------ live events

export type LiveEvent =
  | { type: "job.queued" | "job.cancelled" | "job.failed" | "job.completed"; data: Job }
  | { type: "job.started"; data: { job: Job; video: VideoInfo } }
  | { type: "job.frame"; data: Frame & { job_id: string; progress: number } }
  | { type: "camera.frame"; data: LiveFrame }
  | { type: "camera.status"; data: { camera_id: number; state: CameraStatus["state"]; message: string } }
  | { type: "access.event"; data: AccessEvent }
  | { type: "alert.created" | "alert.updated"; data: Alert };

/**
 * Subscribe to the SSE stream. EventSource reconnects on its own and sends
 * Last-Event-ID, so no events are lost across short drops.
 */
export function subscribe(onEvent: (e: LiveEvent) => void, opts: { job?: string; camera?: number } = {}): () => void {
  const params = new URLSearchParams({ access_token: token ?? "" });
  if (opts.job) params.set("job", opts.job);
  if (opts.camera !== undefined) params.set("camera", String(opts.camera));
  const es = new EventSource(`${base()}/events?${params}`);
  es.onmessage = (m) => {
    try { onEvent(JSON.parse(m.data) as LiveEvent); } catch { /* ignore malformed */ }
  };
  return () => es.close();
}

// ------------------------------------------------------------------ LAN discovery (desktop app only)

export interface FoundServer { name: string; url: string }

/** Asks the desktop shell to browse the LAN (mDNS) for Guard++ servers. Empty in a browser. */
export async function discoverServers(): Promise<FoundServer[]> {
  const invoke = (window as unknown as { __TAURI__?: { core?: { invoke?: (cmd: string) => Promise<unknown> } } }).__TAURI__?.core?.invoke;
  if (!isDesktop || !invoke) return [];
  return (await invoke("discover_servers")) as FoundServer[];
}

/** Absolute URL for a server-relative path such as Community.logo_url. */
export const assetUrl = (path: string) => serverUrl() + path;
