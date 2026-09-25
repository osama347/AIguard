-- guard-core schema v1. Timestamps are UTC 'YYYY-MM-DD HH:MM:SS'.

CREATE TABLE drivers (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    status      TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'inactive', 'blacklisted')),
    phone       TEXT NOT NULL DEFAULT '',
    notes       TEXT NOT NULL DEFAULT '',
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

-- One embedding per enrollment photo. model_version ties templates to the
-- embedding model that produced them (a new model means re-enrollment).
CREATE TABLE face_templates (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    driver_id      INTEGER NOT NULL REFERENCES drivers(id) ON DELETE CASCADE,
    embedding      BLOB NOT NULL,
    model_version  TEXT NOT NULL DEFAULT '',
    created_at     TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX idx_face_templates_driver ON face_templates(driver_id);

CREATE TABLE vehicles (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    plate_number      TEXT NOT NULL,
    plate_normalized  TEXT NOT NULL UNIQUE,
    make              TEXT NOT NULL DEFAULT '',
    model             TEXT NOT NULL DEFAULT '',
    color             TEXT NOT NULL DEFAULT '',
    status            TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'inactive', 'blacklisted')),
    created_at        TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at        TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE assignments (
    driver_id   INTEGER NOT NULL REFERENCES drivers(id) ON DELETE CASCADE,
    vehicle_id  INTEGER NOT NULL REFERENCES vehicles(id) ON DELETE CASCADE,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (driver_id, vehicle_id)
);

CREATE TABLE jobs (
    id             TEXT PRIMARY KEY,
    status         TEXT NOT NULL CHECK (status IN ('queued', 'running', 'completed', 'failed', 'cancelled')),
    source         TEXT NOT NULL,            -- file path (uploads) or stream URL
    original_name  TEXT NOT NULL DEFAULT '',
    size_bytes     INTEGER NOT NULL DEFAULT 0,
    attempts       INTEGER NOT NULL DEFAULT 0,
    progress       REAL NOT NULL DEFAULT 0,  -- 0..1
    verdict        TEXT,
    result         TEXT,                     -- JSON decision + evidence
    error          TEXT,
    created_by     TEXT NOT NULL DEFAULT '',
    created_at     TEXT NOT NULL DEFAULT (datetime('now')),
    started_at     TEXT,
    finished_at    TEXT
);
CREATE INDEX idx_jobs_status ON jobs(status, created_at);

CREATE TABLE access_events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    event_time  TEXT NOT NULL DEFAULT (datetime('now')),
    job_id      TEXT REFERENCES jobs(id) ON DELETE SET NULL,
    verdict     TEXT NOT NULL,
    authorized  INTEGER NOT NULL,
    driver_id   INTEGER,
    vehicle_id  INTEGER,
    plate_text  TEXT NOT NULL DEFAULT '',
    details     TEXT NOT NULL DEFAULT ''     -- JSON
);
CREATE INDEX idx_access_events_time ON access_events(event_time);

CREATE TABLE alerts (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    created_at       TEXT NOT NULL DEFAULT (datetime('now')),
    severity         TEXT NOT NULL CHECK (severity IN ('info', 'warning', 'critical')),
    kind             TEXT NOT NULL,          -- unauthorized | blacklisted | system
    message          TEXT NOT NULL,
    access_event_id  INTEGER REFERENCES access_events(id) ON DELETE SET NULL,
    acknowledged_at  TEXT,
    resolved_at      TEXT
);
CREATE INDEX idx_alerts_open ON alerts(resolved_at, created_at);

CREATE TABLE users (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    username       TEXT NOT NULL UNIQUE,
    password_hash  TEXT NOT NULL,            -- pbkdf2-sha256$<iter>$<salt hex>$<hash hex>
    role           TEXT NOT NULL DEFAULT 'admin' CHECK (role IN ('admin', 'operator')),
    created_at     TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE sessions (
    token_hash  TEXT PRIMARY KEY,            -- sha256 of the bearer token
    user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    expires_at  TEXT NOT NULL
);

CREATE TABLE audit_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    actor       TEXT NOT NULL,
    action      TEXT NOT NULL,
    entity      TEXT NOT NULL,
    entity_id   TEXT NOT NULL DEFAULT '',
    details     TEXT NOT NULL DEFAULT ''
);
