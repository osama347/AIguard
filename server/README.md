# Guard++ server — runs on the NVIDIA Jetson

Guard++ watches the gate cameras around the clock, reads each vehicle's
license plate, recognizes the driver's face, and decides whether that driver
is authorized to use that vehicle. Every passage is logged with a snapshot;
unauthorized ones raise alerts. Everything runs on the Jetson GPU with
TensorRT. Operators connect over the LAN with the Guard++ desktop app
(see `../desktop/`) or any browser.
Uploading a recorded video is available as a testing tool.

```
 Browser / Desktop app ──REST + SSE──► guard-core :8090 ──loopback──► guard-inference :8081
                                       API, rules, SQLite,            cameras (CSI/USB/RTSP) +
                                       camera supervision,            TensorRT models (GPU):
                                       visits, test videos, UI        plate + OCR, face + embedding
```

Design and rationale: [../docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md).
API contract: [../api/openapi.yaml](../api/openapi.yaml).

## Install on the Jetson

Supported: Jetson **Orin** (Nano / NX / AGX) with **JetPack 6.2** (L4T R36.4).

```bash
sudo apt install ./guard-server_2.5.0_arm64.deb        # online: apt fetches missing libraries
sudo bash guard-server-2.5.0-jp6.2-offline.run         # offline: everything bundled
```

Or use the helper, which also prints the connection addresses and the setup code:
`sudo ./install-server.sh [package]`.

The services start at boot and restart on failure. First-time setup asks for your
community (name, address, helpline, logo, ...) and the administrator account. Do it
from the desktop app, or from a browser on the Jetson at `http://localhost:8090`.
From another PC the setup asks for the **setup code** printed at install time
(`sudo cat /etc/guard/guard.env`). The first
start on a new device may spend up to about 20 minutes preparing the AI models.

Then install the **Guard++ desktop app** on any PC on the same network and connect
to `http://<jetson-ip>:8090` (the app also lists Jetsons it finds via mDNS).
A browser pointed at that address works too. Only port 8090 needs to be reachable;
the AI engine listens on localhost only.

| | |
|---|---|
| Services | `systemctl status guard-core guard-inference`, logs: `journalctl -fu guard-core` |
| Configuration | `/etc/guard/core.json` (ports, thresholds), `/etc/guard/inference.json` (models) |
| Data | `/var/lib/guard` (database, snapshots, test videos, engine cache) |
| Uninstall | `sudo apt remove guard` keeps data; `sudo apt purge guard` deletes it |

## Users: administrators and guards

| | Guard | Administrator |
|---|:-:|:-:|
| Dashboard: today's visits, trends, alerts, cameras, system health | – | ✓ |
| Live cameras, access log, snapshots | ✓ | ✓ |
| Alerts: see, acknowledge, resolve (their name is recorded) | ✓ | ✓ |
| Drivers and vehicles | view | manage |
| Cameras, users | – | manage |
| Test videos | – | ✓ |
| Change own password | ✓ | ✓ |

The first administrator is created on the device at first launch. Administrators
land on the **Dashboard**; guards land on **Live**. Administrators
then add guards (and other administrators) under **Users**. Disabling a user or
resetting their password signs them out immediately; there is always at least
one active administrator. Test videos never raise alerts and are hidden from guards.

## Using it

1. **Cameras**: add each gate camera and press *Test connection*:
   | Camera | Source |
   |---|---|
   | Jetson CSI camera | `csi://0` (optional `?width=1920&height=1080&fps=30`) |
   | USB camera | `usb://0` (= `/dev/video0`) |
   | IP camera | `rtsp://user:password@192.168.1.64:554/stream1` (H.264/H.265, hardware-decoded) |
   | Recorded clip, looped (testing) | `file:///var/lib/guard/test-videos/gate.mp4` |
2. **Vehicles**: register plates (spaces, dashes and case are ignored).
3. **Drivers**: add each driver with 2–5 clear, front-facing photos, then
   authorize them for their vehicles.
4. **Live**: every enabled camera is analysed continuously (5 frames/s by
   default). Boxes are drawn over the live picture; each vehicle or person
   passing becomes one event with a verdict: *authorized*, *unauthorized*,
   *unknown driver*, *unknown vehicle* or *nothing recognized*.
5. **Access log** and **Alerts** keep the history, with a snapshot of each
   passage. Unauthorized or blacklisted attempts, and cameras going offline,
   raise alerts that also appear as desktop notifications. Snapshots are kept
   30 days (`live.snapshot_retention_days`); the log itself is kept.
6. **Testing → Test a video**: upload a recorded clip to check the models and
   rules on known footage. It uses exactly the same pipeline as the cameras.

Live tuning lives in `/etc/guard/core.json` under `live`: `visit_gap_s` (a
visit ends after this long with nothing in view), `max_visit_s`,
`min_visit_frames`, `repeat_suppress_s` (the same car waiting at the barrier
stays one event), `offline_alert_s`, `snapshot_retention_days`,
`min_free_disk_mb`. Restart with `sudo systemctl restart guard-core`.

## Development

Requirements on the Jetson: JetPack 6.2 (CUDA, TensorRT, OpenCV), CMake and
Node.js 20+ (for the web UI served by the server). The desktop app is built
separately, see `../desktop/README.md`. Run everything below from `server/`.

```bash
./start.sh                      # build if needed, start both services → http://localhost:8090
./stop.sh
build/core/guard-core-tests     # core unit tests
cd ../frontend && npm run dev   # UI with hot reload on :5173 (proxies /api to :8090)
npm run gen:api                 # regenerate TypeScript types after editing ../api/openapi.yaml
```

Models live in `inference/models/*.onnx`. TensorRT engines are built from them
on first start and cached in `data/engines/` (about 18 minutes on an Orin Nano).

## Building the installers

```bash
packaging/build-deb.sh          # → ../dist/guard-server_2.5.0_arm64.deb
packaging/make-offline.sh       # → ../dist/guard-server-2.5.0-jp6.2-offline.run
```

Build on a JetPack 6.2 Jetson (`JOBS=2` limits parallel compiles). Engines found
in `data/engines/` are shipped as prebuilt engines, so devices with the same
TensorRT version and GPU start in seconds; any other device rebuilds them from the
bundled ONNX files.

## Layout of `server/`

| Path | Contents |
|---|---|
| `core/` | `guard-core`: `api/` HTTP + SSE, `app/` use cases, `domain/` matching and access policy, `infra/` SQLite, inference client, crypto; `migrations/`, `tests/` |
| `inference/` | `guard-inference`: TensorRT engine resolver/builder, perception, single-GPU job queue |
| `packaging/` | Debian package files, systemd units, Avahi (mDNS) announcement, production configs, build scripts |
| `tools/` | Offline evaluation scripts |
| `third_party/` | cpp-httplib, nlohmann/json |
