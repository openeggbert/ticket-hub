-- Phase 1 (reduced scope): local accounts, sessions, minimal login-attempt
-- tracking. See docs/REDUCED_SCOPE_ROADMAP.md Phase 1 and
-- docs/REDUCED_SCOPE_DATA_MODEL.md section B.
--
-- No `handle` column yet (added in Phase 4 with @mentions), no `groups`,
-- `invitations`, `oidc_providers`, or `external_identities` tables (all
-- permanently removed for V1 -- see docs/REMOVED_AND_DEFERRED_FEATURES.md).

ALTER TABLE users DROP COLUMN IF EXISTS username;
ALTER TABLE users ADD COLUMN IF NOT EXISTS time_zone VARCHAR(80) NOT NULL DEFAULT 'UTC';
ALTER TABLE users ADD COLUMN IF NOT EXISTS clock_format VARCHAR(8) NOT NULL DEFAULT '24h'
    CHECK (clock_format IN ('12h', '24h'));
ALTER TABLE users ADD COLUMN IF NOT EXISTS is_admin BOOLEAN NOT NULL DEFAULT FALSE;

CREATE TABLE IF NOT EXISTS local_credentials (
    user_id VARCHAR(36) PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    password_hash TEXT NOT NULL,
    failed_login_count INTEGER NOT NULL DEFAULT 0 CHECK (failed_login_count >= 0),
    locked_until TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS sessions (
    id VARCHAR(36) PRIMARY KEY,
    user_id VARCHAR(36) NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_hash VARCHAR(64) NOT NULL UNIQUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMPTZ NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_sessions_user ON sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires_at);
