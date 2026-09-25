# Guard++ — fleet access control

Guard++ watches gate cameras, reads each vehicle's license plate, recognizes the
driver's face, and decides whether that driver is authorized for that vehicle.
It is split in two independently installed parts:

```
 PC / Mac / Linux (anyone)                       NVIDIA Jetson Orin (background services)
 ┌────────────────────────┐   LAN: REST + SSE   ┌────────────────────────────────────────┐
 │ Guard++ desktop app    │ ──────────────────► │ guard-core :8090  API, rules, SQLite   │
 │ (setup file per OS)    │      port 8090      │      │ loopback                        │
 └────────────────────────┘                     │ guard-inference :8081  TensorRT (GPU)  │
                                                └────────────────────────────────────────┘
```

| Part | Folder | Installed as |
|---|---|---|
| **Server** (AI engine + database + API) | [`server/`](server/README.md) | `guard-server_<ver>_arm64.deb` (or offline `.run`) on the Jetson; systemd services |
| **Desktop app** (client) | [`desktop/`](desktop/README.md) | `.msi`/`.exe`, `.dmg`, `.deb`/`.AppImage` per OS, built by CI |
| Shared web UI | [`frontend/`](frontend) | bundled into the desktop app; also served by the server for browsers |
| API contract | [`api/openapi.yaml`](api/openapi.yaml) | |

Design and rationale: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
