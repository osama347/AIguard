-- guard-core schema v5: every vehicle has exactly one owner, who is a driver
-- (is_owner = 1) auto-enrolled and authorized to drive their own vehicle.
-- Owners get a reusable authorization code an admin can redeem later to add
-- further authorized drivers, without the owner needing to be present.
--
-- owner_id/auth_code are nullable: existing vehicles keep working unchanged
-- after this upgrade (the app enforces "must have an owner" for new vehicles
-- and offers to backfill legacy ones, rather than a forced bulk migration).

ALTER TABLE drivers ADD COLUMN is_owner INTEGER NOT NULL DEFAULT 0;
ALTER TABLE vehicles ADD COLUMN owner_id INTEGER REFERENCES drivers(id);
-- SQLite won't take a UNIQUE constraint on ADD COLUMN, so it's a separate index;
-- a unique index still allows any number of NULLs (vehicles without an owner yet).
ALTER TABLE vehicles ADD COLUMN auth_code TEXT;
CREATE UNIQUE INDEX idx_vehicles_auth_code ON vehicles(auth_code);
