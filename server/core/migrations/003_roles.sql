-- guard-core schema v3: two kinds of users.
--   admin  manages everything (cameras, drivers, vehicles, users, test videos)
--   guard  monitors: live view, access log, alerts (acknowledge/resolve), read-only fleet
-- Users can be disabled instead of deleted; alerts record who handled them.
-- The role CHECK changes, so the users table is rebuilt. Everyone signs in again.

DELETE FROM sessions;

CREATE TABLE users_v3 (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    username       TEXT NOT NULL UNIQUE,
    full_name      TEXT NOT NULL DEFAULT '',
    password_hash  TEXT NOT NULL,            -- pbkdf2-sha256$<iter>$<salt hex>$<hash hex>
    role           TEXT NOT NULL DEFAULT 'guard' CHECK (role IN ('admin', 'guard')),
    active         INTEGER NOT NULL DEFAULT 1,
    created_at     TEXT NOT NULL DEFAULT (datetime('now')),
    last_login_at  TEXT
);
INSERT INTO users_v3(id, username, password_hash, role, created_at)
    SELECT id, username, password_hash, CASE role WHEN 'admin' THEN 'admin' ELSE 'guard' END, created_at FROM users;
DROP TABLE users;
ALTER TABLE users_v3 RENAME TO users;

ALTER TABLE alerts ADD COLUMN acknowledged_by TEXT NOT NULL DEFAULT '';
ALTER TABLE alerts ADD COLUMN resolved_by TEXT NOT NULL DEFAULT '';
