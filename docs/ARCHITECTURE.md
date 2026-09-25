# Guard++ Target Architecture

Status: **implemented** · updated 2026-09-26 for the server/desktop split

> **Deployment split (2026-09-26).** Guard++ ships as two independently installed
> parts. The **server** (`guard-core` + `guard-inference` + SQLite + models) is a
> package for the Jetson that runs as background systemd services and is reachable
> over the LAN on port 8090. The **desktop app** is a cross-platform client
> (Windows, macOS, Linux) installed from a per-OS setup file; it holds no AI or
> data and only talks to the server's HTTP API. Where the older text below says
> "one package"/"desktop app on the device", this note takes precedence; sections
> 8 and 9 are current.

## 1. Context and constraints

Guard++ decides whether a driver is authorized to use a vehicle, by reading the
plate and recognizing the driver's face from video.

| Constraint | Consequence for the design |
|---|---|
| Runs on one NVIDIA Jetson (8 GB shared CPU/GPU RAM, TensorRT 10.3) | One GPU, one host: prefer few processes, local IPC, embedded storage. RAM is scarce, so no heavy runtimes next to the models |
| Model loading takes seconds; TensorRT/CUDA faults can kill a process | Keep GPU work in its own process so a crash or restart doesn't take the API down |
| GPU is the bottleneck and the TensorRT code has no locking | GPU work must be **serialized through one queue**, not run from one thread per request |
| Browser UI now, **desktop app later** | UI must be a client of a stable API. Build it once and ship it to both targets |
| **Live cameras analysed 24/7** are the product; video upload stays as a testing tool | Pipeline input is a *source* (CSI, USB, RTSP, file); continuous streams are split into visits, each decided and logged like an uploaded clip |
| **Two setup files**: a server package for Jetsons of the same kind, and a desktop installer for any operator PC | Server: services, models, config, dependencies in one versioned package. TensorRT engines are device-specific, so they must be validated or rebuilt on the target. Desktop: one installer per OS, no AI or data |

## 2. Decision summary

1. **Two backend processes**: `guard-core` (the only public API, business
   logic and storage) and `guard-inference` (GPU perception only, localhost only).
2. **The database becomes a library inside `guard-core`** (embedded SQLite)
   instead of a separate REST service.
3. **Inference does perception, core does decisions.** Inference returns
   detections, OCR text and face embeddings. Core does gallery matching, the
   authorization verdict, and records events.
4. **Analysis runs as persisted jobs** with a single GPU queue and a replayable
   event stream.
5. **One frontend codebase** (TypeScript web app), served by core for browsers
   and packaged with **Tauri** for the desktop. The desktop shell contains no
   business logic.
6. **The HTTP API is the contract** (REST + Server-Sent Events, described in
   OpenAPI), and the frontend's types are generated from it.
7. **Two deliverables.** The server is one Debian package,
   `guard-server_<version>_arm64.deb` (both services, models, systemd units, mDNS
   announcement); an **offline installer** (`guard-server-<version>-jp6.2-offline.run`)
   bundles it with every dependency. The desktop app is a Tauri installer per OS,
   built by CI. See section 8.
8. **Live cameras are the primary input.** Inference keeps one long-running
   *stream* per camera (capture + analysis threads, always on the newest
   frame); core supervises each camera, splits the continuous detections into
   **visits**, decides each visit with the same policy as a clip, and logs it
   with a snapshot. Uploaded videos go through the same annotator and policy
   as a secondary testing path.

## 3. Components

```
 ┌──────────────── clients ─────────────────┐
 │  Browser            Desktop (Tauri)      │   same frontend build (frontend/)
 └──────────┬──────────────────┬────────────┘   desktop runs on operator PCs, over the LAN
            │  HTTP  REST + SSE  (Bearer token)
            ▼                  ▼
 ┌──────────────────────────────────────────────┐
 │ guard-core                     :8090 (public)│
 │  api/        HTTP handlers, auth, static UI  │
 │  app/        CameraService, JobService,      │
 │              FleetService, EventBus          │
 │  domain/     Camera, Driver, Vehicle, Job,   │
 │              FaceMatcher, AccessPolicy,      │
 │              VisitTracker                    │
 │  infra/      SqliteRepos, InferenceClient,   │
 │              MediaStore (data/media)         │
 │  SQLite  data/guard.db  (embedded, WAL)      │
 └───────────────┬──────────────────────────────┘
                 │ loopback HTTP + SSE  (127.0.0.1:8081)
                 │ jobs reference files by path (same host)
                 ▼
 ┌──────────────────────────────────────────────┐
 │ guard-inference            127.0.0.1 only    │
 │  StreamManager: 1 capture + 1 analysis       │
 │    thread per camera (csi/usb/rtsp/file)     │
 │  JobQueue: test videos (1 worker)            │
 │  Perception (GPU lock): face→embed, plate→ocr│
 │  ModelManager + TensorRT engines             │
 └──────────────────────────────────────────────┘
```

### guard-core (new, C++; absorbs `database/`)
Owns **everything that is not GPU work**:
- Public REST API and SSE event stream. Also serves the built frontend for browsers.
- Authentication (users, tokens) and authorization.
- Fleet management: drivers, face templates, vehicles, assignments.
- Cameras: one supervisor per enabled camera keeps its inference stream
  registered, consumes frames, tracks visits, records access events with
  snapshots, and raises/resolves "camera offline" alerts.
- Test videos (jobs): accepts uploads into `data/media/`, persists a job row,
  queues it, sends it to inference, consumes the detection stream, and writes results.
- **Decision logic**: face-to-gallery matching (cosine against stored templates),
  plate normalization and lookup, the verdict policy, and writing
  `access_events` and `alerts`.
- Supervision: reports inference health and retries jobs when inference restarts.

Internal layering (dependencies point inward only):

```
api  ──►  app  ──►  domain
 │         │
 └──►  infra (implements domain/app interfaces: repositories, InferenceClient)
```

- `domain/` is plain C++ with no HTTP, SQL or JSON. Unit-testable.
- `app/` holds the use cases (`EnrollDriver`, `SubmitJob`, `HandleDetections`).
- `infra/` holds the adapters: SQLite repositories (from today's
  `database/src/database.cpp` + migrations), the HTTP client to inference, and file storage.
- `api/` holds HTTP routing, request/response mapping and auth middleware.

### guard-inference (today's `ai-engine/`, slimmed)
Owns **only the GPU** and knows nothing about drivers, vehicles, verdicts, users or UI.
- Input: a job `{job_id, source, sample_fps, pipelines}`, where `source` is
  `file:///…` today and `rtsp://…` later. Also single images (enrollment photos).
- Output: an SSE stream of per-frame perception results:
  `{frame, t_ms, faces:[{bbox, conf, embedding}], plates:[{bbox, conf, text, text_conf}]}`.
- One `WorkQueue` with a single GPU worker. Requests are queued rather than each
  spawning a detached thread. This removes the current race on shared TensorRT contexts.
- Binds to `127.0.0.1`, with a shared secret header from core.
- Later: hardware decoding (NVDEC via GStreamer) to replace CPU `cv::VideoCapture`.

### frontend/ (TypeScript + React + Vite)
- A single-page app. Every call goes to `guard-core` through a generated, typed API client.
- The server URL is configurable. In the browser it's the page's own origin; in
  the desktop app the user picks the Jetson (from the mDNS device list or by typing its address).
- Screens: **Dashboard** (admins' landing page), **Live** (camera grid with boxes, recent events), Access log (with
  snapshots and filters), Alerts, Drivers, Vehicles, Cameras, and under
  *Testing*: Test a video (upload, overlay, verdict) and Test results.

### desktop/ (Tauri 2)
- A thin shell that loads the `frontend/` build. Native extras only: window,
  OS notifications for alerts, LAN discovery of servers (one Rust command that
  browses mDNS `_guard._tcp`, announced by the server package), remembered server URL.
- **No API calls from Rust and no credentials in the shell.** This avoids the
  earlier Tauri prototype's problems: it bypassed the backend and had a hardcoded login.
- Installers for Windows, macOS and Linux are built by CI (`.github/workflows/desktop.yml`).

## 4. Contracts

**Public API (core, `/api/v1`)**, described in `api/openapi.yaml`:

| Resource | Endpoints |
|---|---|
| Auth | `POST /auth/login` → token, `POST /auth/logout`, `POST /auth/password` |
| Dashboard (admin) | `GET /dashboard` (today vs yesterday, 24 h and 14 day series by verdict, unregistered plates, frequent drivers, alert response, camera and system health) |
| Users (admin) | `GET/POST /users`, `PUT/DELETE /users/{id}` |
| Drivers | `GET/POST /drivers`, `GET/PUT/DELETE /drivers/{id}`, `POST /drivers/{id}/photos` (enroll face) |
| Vehicles | `GET/POST /vehicles`, `GET/PUT/DELETE /vehicles/{id}` |
| Assignments | `PUT/DELETE /drivers/{id}/vehicles/{vid}` |
| Cameras | `GET/POST /cameras`, `GET/PUT/DELETE /cameras/{id}`, `POST /cameras/probe`, `GET /cameras/{id}/live.mjpeg`, `GET /cameras/{id}/preview.jpg` |
| Jobs (test videos) | `POST /jobs` (multipart video) → `{id}`, `GET /jobs`, `GET /jobs/{id}`, `POST /jobs/{id}/cancel`, `DELETE /jobs/{id}` |
| Access events | `GET /access-events?source=&camera_id=&verdict=`, `GET /access-events/{id}/snapshot.jpg`, `GET /alerts`, `POST /alerts/{id}/acknowledge` |
| System | `GET /health` (core + inference + disk), `GET /system/models` |

**Event stream**: `GET /api/v1/events[?job=<id>|?camera=<id>]` (SSE). Event types:
`job.*` (queued, started, frame, completed, failed, cancelled), `camera.frame`
(boxes + labels, already matched, ~5/s per camera), `camera.status`,
`access.event`, `alert.created`, `alert.updated`. Camera frames go to a small
separate buffer so they never push durable notifications out of the replay window. Every event has a monotonically increasing
`id`, so a client reconnecting with `Last-Event-ID` gets what it missed. The desktop
app can sleep or reconnect without losing results.

**Why SSE rather than WebSocket:** the traffic is one-way (server to client) and
commands go over REST. SSE reconnects automatically, passes through proxies,
and cpp-httplib already supports it. No extra library is needed.

**Internal API (core → inference)**: jobs (`POST /v1/jobs`, `GET /v1/jobs/{id}/stream`,
`DELETE /v1/jobs/{id}`), live streams (`PUT /v1/streams/{id}` idempotent,
`GET /v1/streams/{id}/events`, `/preview.jpg?after=N`, `/frames/{n}.jpg`,
`DELETE`), `POST /v1/probe`, `POST /v1/embed` (images → embeddings), `GET /v1/health`.
It is versioned separately and only core uses it.

## 5. Key flows

**Test video analysis** (secondary, for testing)
1. Client `POST /jobs` with the video. Core stores it at `data/media/<job>.mp4`,
   inserts `jobs(status=queued)`, and returns the id.
2. Core's job runner submits `{job_id, source:file://…}` to inference.
3. Inference decodes, samples about 5 fps, runs both pipelines, and streams perception results.
4. For each frame, core matches embeddings against the in-memory gallery cache
   (rebuilt when fleet data changes), normalizes plates, and publishes `job.frame`.
5. At the end, core applies `AccessPolicy` to the aggregated evidence, then writes
   the job result, an `access_events` row and, if unauthorized, an `alerts` row.
   It publishes `job.completed`.
6. If inference dies mid-job, core marks the job `retrying` and resubmits it once
   inference is healthy (bounded attempts).

**Live camera monitoring (24/7)**
1. An admin adds a camera (`csi://0`, `usb://0`, `rtsp://…`, or a looped
   `file://` clip for testing). Core starts a supervisor for it.
2. The supervisor waits for inference to be healthy and `PUT`s the stream
   (idempotent: after either service restarts it simply re-registers).
3. Inference's capture thread reads frames as fast as the camera delivers them,
   keeping only the newest (GStreamer + `nvv4l2decoder` for RTSP,
   `nvarguscamerasrc` for CSI, V4L2 for USB), and reconnects with backoff. The
   analysis thread takes the newest frame `sample_fps` times per second, runs
   perception under the GPU lock (interleaving with test videos and enrollment),
   and emits a frame event. Frames with detections are also kept as JPEGs for a
   few seconds; preview JPEGs are encoded only while someone is watching.
4. Core annotates each frame (face matching, plate lookup), publishes
   `camera.frame`, and feeds the `VisitTracker`: a visit opens on the first
   detection and closes after `visit_gap_s` (4 s) with nothing in view, or is
   split after `max_visit_s`. Visits with fewer than `min_visit_frames`
   detections are noise.
5. At the end of a visit core runs `AccessPolicy` over the visit's evidence
   (same as a whole clip), saves the best frame as the snapshot
   (`media/snapshots/<date>/`), writes the `access_events` row (camera, start,
   end, overlay boxes) and alerts, and publishes `access.event`. The same outcome
   again within `repeat_suppress_s` (a car waiting at the barrier) extends the
   previous event instead of logging a new one.
6. A monitor thread raises a `camera_offline` alert after `offline_alert_s`
   (not while the AI engine itself is starting) and resolves it when frames
   return. Snapshots older than `snapshot_retention_days`, or the oldest days
   when disk runs low, are deleted; access log rows are kept.

**Driver enrollment**: client uploads photos → core → inference `/v1/embed`
→ core stores templates → gallery cache invalidated.

## 6. Data

- One SQLite file `data/guard.db` in WAL mode, owned by core. Migrations stay as
  numbered SQL files in `core/migrations/` and are applied at startup.
- New tables: `jobs`, `job_detections` (per-frame summary, optional retention),
  `users`, `sessions`. The existing `access_events`, `alerts` and `audit_logs`
  tables finally get written.
- `cameras` (schema v2) holds each camera's source and rate; `access_events`
  gained `camera_id`, `ended_at` and `snapshot` so one log covers cameras and test videos.
- Media lives on disk under `data/media/`, not in SQLite: uploaded test videos
  (deleted with their job) and visit snapshots under `media/snapshots/<UTC date>/`
  (deleted after `snapshot_retention_days` or when free disk drops below
  `min_free_disk_mb`).
- Face embeddings are stored as BLOBs next to the `model_version` that produced
  them. Changing the embedding model means re-enrolling (already implied by the schema).

## 7. Cross-cutting

- **Security**: core is the only listener on the network. Inference is loopback
  plus a shared secret. Bearer tokens for clients. Passwords hashed. No default credentials.
- **Roles** (schema v3): `admin` manages cameras, fleet, users and test videos;
  `guard` monitors (live view, access log, alerts with acknowledge/resolve
  recorded by name) and sees the fleet read-only. Enforced in core on every
  endpoint (403), including filtering test-video events out of guards' SSE
  streams and access log; the UI only mirrors it. At least one active admin
  always remains; disabling a user or resetting a password revokes their sessions.
- **Configuration**: `core.json` (ports, paths, thresholds, retention) and
  `inference.json` (models, precision). Match threshold and verdict rules live in
  **core** config, because they're policy, not perception.
- **Observability**: structured logs from both processes. `GET /health` aggregates
  status. Per-stage timings from inference are included in the job result.
- **Deployment**: two systemd units, `guard-core` and `guard-inference`, with
  `Restart=always`, running as the unprivileged system user `guard` (groups
  `video`, `render` for GPU and cameras; inference sees the real `/tmp` because
  CSI cameras reach `nvargus-daemon` through `/tmp/argus_socket`). Core doesn't
  depend on inference to start; it reports it as unavailable. Installed by the
  package described in section 8.
- **Testing**: domain unit tests (matcher, policy, plate normalization), repository
  tests against a temp SQLite file, and an inference contract test with a mock
  inference server (the existing `examples/mock_server.cpp` becomes this). Frontend
  tests use a mocked API client.

## 8. Packaging and distribution

### Target platform ("same kind of Jetson")
| | Supported |
|---|---|
| Hardware | Jetson **Orin** family (Orin Nano / Orin NX / AGX Orin, GPU sm_87). Reference device: Orin Nano Super |
| OS | **JetPack 6.2.x = L4T R36.4.x** (Ubuntu 22.04, CUDA 12.6, TensorRT 10.3) |

The installer checks this before touching anything (`/etc/nv_tegra_release`,
`/proc/device-tree/model`) and refuses a different L4T major/minor. CUDA,
TensorRT and the GPU drivers are part of JetPack and tied to the L4T kernel, so
they are **declared as dependencies, never bundled or replaced** by our package.
Shipping them would break the device's JetPack and NVIDIA's license terms.

### Deliverables
1. **`guard-server_<version>_arm64.deb`**, the Jetson package. Install:
   `sudo apt install ./guard-server_2.5.0_arm64.deb`. apt pulls any missing
   requirements from the Ubuntu and NVIDIA JetPack repositories. It replaces the
   former all-in-one `guard` package (`Conflicts/Replaces: guard`).
2. **`guard-server-<version>-jp6.2-offline.run`**, a self-extracting installer
   containing the `.deb` plus every dependency `.deb` that a stock JetPack 6.2
   image lacks. Uses a local apt repository. For gates without internet.
3. **Desktop installers** (`desktop/`): `.msi`/`.exe` (Windows), `.dmg` (macOS),
   `.deb`/`.AppImage` (Linux), built by GitHub Actions with the Tauri bundler.

### Package contents
```
/opt/guard/bin/guard-core, guard-inference     services
/opt/guard/share/frontend/                     same UI, served by core for browsers
/opt/guard/share/migrations/                   SQL migrations
/opt/guard/models/*.onnx                       source models (always shipped)
/opt/guard/models/prebuilt/*.engine + manifest.json   engines + {device, TRT version}
/etc/guard/core.json, inference.json           conffiles: kept across upgrades
/lib/systemd/system/guard-core.service, guard-inference.service
/etc/avahi/services/guard.service            announces _guard._tcp:8090 on the LAN (mDNS)
/var/lib/guard/{guard.db, media/, engines/}    created at install, owned by user guard
```

### Dependencies (`Depends:`)
`nvidia-l4t-core (>= 36.4), nvidia-l4t-core (<< 36.5), libnvinfer10 (>= 10.3),
libnvonnxparsers10 (>= 10.3), cuda-cudart-12-6, libopencv (>= 4.8), libsqlite3-0,
libstdc++6, adduser, systemd` (Recommends: `avahi-daemon`)

`libopencv` 4.8 is NVIDIA's JetPack build (the binaries link `libopencv_*.so.408`),
not Ubuntu's `libopencv-*4.5d`.

### Maintainer scripts
- **preinst**: platform check (above). Stops and disables the legacy
  `/opt/guard++` units (`guard-ai-engine`, `guard-database`) if present.
- **postinst**: create the `guard` system user (groups `video`, `render` for GPU
  access). Create `/var/lib/guard`. Generate the core↔inference shared secret
  once into `/etc/guard/guard.env` (read by both units). `systemctl daemon-reload`,
  then enable and (re)start `guard-inference` and `guard-core`.
- **prerm**: stop and disable the services.
- **postrm purge**: remove `/var/lib/guard`, `/etc/guard` and the `guard`
  user. A plain *remove* keeps data, so reinstalling keeps the fleet database.

The package is named `guard-server` (version **2.5.0**). It replaces the former `guard` package, which also contained the desktop app.

### TensorRT engines on a new device
`.engine` files are only valid for the GPU and TensorRT build that created
them. On startup `guard-inference` resolves each model like this:
1. A cached engine in `/var/lib/guard/engines/` whose recorded
   TensorRT version and device match → use it.
2. Else a prebuilt engine from `/opt/guard/models/prebuilt/` whose manifest
   matches → copy it to the cache.
3. Else build from the `.onnx` with the TensorRT builder API (FP16) → cache it.
   Measured on the Orin Nano Super: about 18 minutes for all four models; with
   matching prebuilt engines, models are ready in about 4 seconds. While
   building, `/v1/health` reports `loading` with a progress message, which the
   app shows as "AI engine preparing…".

Each engine has a sidecar `<key>.engine.json` recording
`{trt_version, device, onnx_hash, precision}`; a mismatch in any field means rebuild.

### Desktop app and the LAN
- The server listens on `0.0.0.0:8090` (`server.host` in `/etc/guard/core.json`);
  only that port needs to be reachable. `guard-inference` stays on loopback.
- The desktop app starts with no server selected and shows a connection screen:
  Jetsons found via mDNS (`_guard._tcp`, from `/etc/avahi/services/guard.service`),
  or a typed address such as `http://192.168.1.20:8090`. The choice is remembered.
- **First run**: no default credentials. While no user exists, the app shows a
  setup screen: the **community profile** (name, address, city, country, helpline,
  email, website, logo; stored in the `community` table, logo in `media/branding/`,
  shown on the login screen and in the header) and the first **administrator**.
  `POST /api/v1/setup` works from localhost without a code; from another machine it
  needs the **setup code** the installer generated (`GUARD_SETUP_CODE` in
  `/etc/guard/guard.env`, printed at install time, throttled to 5 wrong tries per
  5 minutes, useless once the first account exists). Admins can edit the profile later in Settings.
- Traffic is plain HTTP (Bearer tokens, video). TLS with a pinned self-signed
  certificate is a planned follow-up; until then, keep the server on a trusted LAN.

### Upgrades
Install a newer `.deb` over the old one. Config files in `/etc/guard` are kept.
Core backs up `guard.db` to `guard.db.bak-v<schema>` before applying new
migrations at startup. Engine caches are invalidated automatically when the
TensorRT version changes.

### Build pipeline
**Server** (`server/packaging/build-deb.sh`) runs **natively on a JetPack 6.2
Jetson**; there is no cross-compiling, because CUDA and TensorRT must match the target.
1. `cmake --build` → `guard-core`, `guard-inference` (Release, TensorRT on); unit tests.
2. `npm ci && npm run build` in `frontend/` → `dist/` (the browser UI the server also serves).
3. Assemble the tree above, then `dpkg-deb --build` (full control over the maintainer scripts).
4. `server/packaging/make-offline.sh` → download the missing dependency `.deb`s → `.run`.

**Desktop** (`desktop/`): `npm run build` builds `frontend/` and runs `tauri build`.
Windows/macOS/Linux installers come from the CI matrix in `.github/workflows/desktop.yml`.

## 9. Repository layout

```
Guard++/
  server/        everything installed on the Jetson
    core/          guard-core   (api/ app/ domain/ infra/ migrations/ tests/)
    inference/     guard-inference (models/ config/)
    packaging/     debian/, systemd units, avahi/, configs, build-deb.sh, make-offline.sh
    third_party/   cpp-httplib, nlohmann/json
    tools/         offline evaluation scripts
  desktop/       Tauri shell (src-tauri/), bundles ../frontend/dist
  frontend/      React + TS + Vite app, shared by the desktop app and the server's browser UI
  api/           openapi.yaml (source of truth for the public API)
  docs/          this file
  .github/       desktop installer CI
```

## 10. What changes from today, and why

| Today | Problem | Target |
|---|---|---|
| Browser → ai-engine, which also serves the UI and proxies `/db/*` | The GPU process is also the public web server; one crash takes everything down | Core is the public server; inference is private |
| Separate `guard-database` REST process | Extra hop, extra auth, extra process to supervise, for a SQLite file on the same box | SQLite embedded in core through repositories |
| Verdict and matching computed in `pipeline_manager.cpp` | Business rules inside the GPU engine; the whole gallery is sent with every upload | Inference returns embeddings; core matches and decides |
| One detached thread per upload, no locking around TensorRT | Two concurrent uploads race on shared execution contexts | One GPU work queue |
| Sessions only in memory, never cleaned up; one SSE consumer | Results are lost on restart or reconnect; memory leak | Persisted jobs; replayable events |
| `access_events` and `alerts` tables unused | No history or audit | Every job writes its outcome |
| Plain JS page | Won't scale to the desktop app | Typed frontend shared by browser and Tauri |

## 11. Migration plan (each step leaves a working system)

All steps are done.

1. **Core skeleton**: create `core/` and move `database_server.cpp` routes into
   core with embedded SQLite (reuse `guard_fleet_core`). Core serves `web/` and
   proxies uploads to inference. Remove `web_ui.cpp` from inference and bind inference to localhost.
2. **Packaging skeleton**: `packaging/` producing the `.deb` for core +
   inference (platform check, user, systemd units, engine resolution). From here
   on, every step is verified by installing on a clean Jetson.
3. **Jobs + queue**: `jobs` table, job runner, persisted replayable events;
   `WorkQueue` in inference.
4. **Move decisions**: inference emits embeddings; core gets `FaceMatcher` +
   `AccessPolicy` (+ unit tests) and writes access events and alerts.
5. **Frontend**: `api/openapi.yaml`, then the React/TS app with a generated
   client, replacing `web/`. Add auth.
6. **Desktop**: Tauri shell around `frontend/dist`, with server-URL setting,
   first-run setup and notifications, added to the `.deb`. Then the offline `.run` installer.
7. **Live sources** (done): CSI/USB/RTSP/file camera streams in inference with
   hardware decoding, camera supervisors and visit tracking in core, Live and
   Cameras screens; uploads kept as the testing path.

## 12. Alternatives considered

- **Keep three services (AI, DB, backend) like the old design.** Rejected: the
  DB process adds latency and operational burden with no isolation benefit on a
  single device. SQLite is meant to be embedded.
- **Single monolith (inference inside core).** Rejected: model load time and
  CUDA faults would take the API down, and core couldn't report "inference down"
  or retry jobs.
- **Qt/QML desktop app.** Native and light, but it means a second UI codebase
  alongside the browser UI. Tauri reuses the web frontend and uses the system
  WebKit, so RAM cost stays low.
- **Electron.** Rejected: bundles Chromium plus Node, too heavy next to the
  models in 8 GB shared memory.
- **gRPC between core and inference.** Better typing and streaming, but adds
  protobuf/gRPC build weight on Jetson. Loopback HTTP + SSE is enough at about
  5 fps × a few faces. Revisit if moving raw frames between processes.
- **Tauri's own bundler or CPack for the installer.** Tauri's bundler targets
  the desktop app, so extra services and systemd units feel bolted on. CPack
  is fine, but it's a thin layer over the same `debian/` files. Plain `dpkg-deb`
  with explicit maintainer scripts is the most transparent option.
- **Snap, Flatpak, AppImage or Docker.** They sandbox away from the host
  JetPack CUDA/TensorRT, or need the NVIDIA container runtime with large images
  on a small eMMC/SD device. A native `.deb` fits a system service that needs the GPU.
- **Python (FastAPI) for core.** Faster to write CRUD, but it would split the
  stack and duplicate the existing C++ storage layer. Stay with C++17 + cpp-httplib.
