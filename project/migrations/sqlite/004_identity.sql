-- Phase 1 (reduced scope): local accounts, sessions, minimal login-attempt
-- tracking. See docs/REDUCED_SCOPE_ROADMAP.md Phase 1 and
-- docs/REDUCED_SCOPE_DATA_MODEL.md section B.
--
-- No `handle` column yet (added in Phase 4 with @mentions), no `groups`,
-- `invitations`, `oidc_providers`, or `external_identities` tables (all
-- permanently removed for V1 -- see docs/REMOVED_AND_DEFERRED_FEATURES.md).
--
-- SQLite's ALTER TABLE DROP COLUMN refuses to drop a column that
-- participates in a UNIQUE constraint (`username` here), so this migration
-- rebuilds the table instead, following SQLite's own documented procedure.
-- The migration runner (SqliteDatabase::migrate()) disables and re-verifies
-- foreign keys around every migration for exactly this reason.

CREATE TABLE users_new (
    id VARCHAR(36) PRIMARY KEY,
    display_name VARCHAR(160) NOT NULL,
    email VARCHAR(320) NOT NULL UNIQUE,
    avatar_url TEXT,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    time_zone TEXT NOT NULL DEFAULT 'UTC',
    clock_format TEXT NOT NULL DEFAULT '24h' CHECK (clock_format IN ('12h', '24h')),
    is_admin INTEGER NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO users_new(id, display_name, email, avatar_url, active, created_at, updated_at)
SELECT id, display_name, email, avatar_url, active, created_at, updated_at FROM users;

DROP TABLE users;
ALTER TABLE users_new RENAME TO users;

CREATE TABLE IF NOT EXISTS local_credentials (
    user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    password_hash TEXT NOT NULL,
    failed_login_count INTEGER NOT NULL DEFAULT 0 CHECK (failed_login_count >= 0),
    locked_until TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_hash TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_sessions_user ON sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires_at);
