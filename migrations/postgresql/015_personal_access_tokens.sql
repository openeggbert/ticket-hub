-- Personal access tokens (Phase 6, D39/D40): PAT-only API authentication,
-- no service accounts and no webhooks. D40's "basic token security": hashed
-- storage (never the raw token), expiration, revocation, last-used
-- tracking -- no scopes (a token carries exactly its owner's permissions),
-- no rotation, no admin-configurable max lifetime. Mirrors `sessions`
-- almost exactly, plus `name` (a user-chosen label to tell tokens apart),
-- `revoked_at` (a token can be revoked before it naturally expires), and
-- `last_used_at` (nullable -- never used yet).

CREATE TABLE IF NOT EXISTS personal_access_tokens (
    id VARCHAR(36) PRIMARY KEY,
    user_id VARCHAR(36) NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name VARCHAR(120) NOT NULL,
    token_hash VARCHAR(64) NOT NULL UNIQUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMPTZ NOT NULL,
    last_used_at TIMESTAMPTZ,
    revoked_at TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS idx_pat_user ON personal_access_tokens(user_id);
CREATE INDEX IF NOT EXISTS idx_pat_expires ON personal_access_tokens(expires_at);
