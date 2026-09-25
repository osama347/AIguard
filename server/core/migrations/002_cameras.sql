-- guard-core schema v2: live cameras. Access events now come from either an
-- uploaded test video (job_id) or a live camera (camera_id).

CREATE TABLE cameras (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    source      TEXT NOT NULL,            -- csi://0, usb://0, rtsp://..., file://... (see docs)
    enabled     INTEGER NOT NULL DEFAULT 1,
    sample_fps  REAL NOT NULL DEFAULT 5,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

ALTER TABLE access_events ADD COLUMN camera_id INTEGER REFERENCES cameras(id) ON DELETE SET NULL;
ALTER TABLE access_events ADD COLUMN ended_at TEXT;
ALTER TABLE access_events ADD COLUMN snapshot TEXT NOT NULL DEFAULT '';   -- path under media/, '' if none
CREATE INDEX idx_access_events_camera ON access_events(camera_id, event_time);
