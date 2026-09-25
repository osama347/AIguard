-- guard-core schema v4: the community this installation serves (name, contact details, logo).
-- One row (id = 1), created empty. Shown on the login screen and in the app header.

CREATE TABLE community (
    id          INTEGER PRIMARY KEY CHECK (id = 1),
    name        TEXT NOT NULL DEFAULT '',
    address     TEXT NOT NULL DEFAULT '',
    city        TEXT NOT NULL DEFAULT '',
    country     TEXT NOT NULL DEFAULT '',
    helpline    TEXT NOT NULL DEFAULT '',
    email       TEXT NOT NULL DEFAULT '',
    website     TEXT NOT NULL DEFAULT '',
    logo_file   TEXT NOT NULL DEFAULT '',     -- file name inside <data>/media/branding/, '' = no logo
    updated_at  TEXT NOT NULL DEFAULT (datetime('now'))
);
INSERT INTO community(id) VALUES(1);
