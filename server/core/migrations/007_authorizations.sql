-- guard-core schema v7: owner-approved driver authorizations, plus a small
-- key/value settings table.
--
-- Instead of an admin redeeming the owner's reusable code on the owner's behalf,
-- an admin now *requests* an authorization: the driver's details are captured up
-- front, a single-use token is minted, and the vehicle's owner must confirm
-- out-of-band (a WhatsApp link, GET /confirm/<token>) before the driver is
-- created and assigned. Each request carries its own token, expiry and decision,
-- so a later request for another driver is completely independent.
--
-- token_hash is the SHA-256 of the token that travels in the link; the raw token
-- is only ever emailed/shown once and never stored (a leaked database backup does
-- not hand out live capabilities).

CREATE TABLE driver_authorizations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id INTEGER NOT NULL REFERENCES vehicles(id) ON DELETE CASCADE,
    driver_name TEXT NOT NULL,
    driver_phone TEXT NOT NULL DEFAULT '',
    token_hash TEXT NOT NULL UNIQUE,
    status TEXT NOT NULL DEFAULT 'pending',   -- pending | approved | denied | cancelled | expired
    photo_dir TEXT NOT NULL DEFAULT '',       -- staged face photos under <data>/media/pending/, "" = none
    requested_by TEXT NOT NULL DEFAULT '',
    authorized_driver_id INTEGER REFERENCES drivers(id),
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    expires_at TEXT NOT NULL,
    decided_at TEXT
);
CREATE INDEX idx_driver_authorizations_vehicle ON driver_authorizations(vehicle_id, status);
CREATE INDEX idx_driver_authorizations_status ON driver_authorizations(status);

-- Deployment/runtime settings that don't belong to a single entity: today the
-- public base URL used to build owner-approval links (an ngrok/Cloudflare tunnel
-- can change it at will), and how long an authorization stays valid.
CREATE TABLE app_settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL DEFAULT '',
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);
