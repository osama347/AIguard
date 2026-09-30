-- guard-core schema v6: an optional profile picture per person, separate from the
-- biometric face-enrollment photos (those are only ever embedded, never stored).
-- Admin-managed for now, mirroring the community logo column.
ALTER TABLE drivers ADD COLUMN photo_file TEXT NOT NULL DEFAULT '';
