-- @handle mentions (D56/D80) and the fixed in-app notification set (D14).

ALTER TABLE users ADD COLUMN IF NOT EXISTS handle VARCHAR(32);
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_handle ON users(handle) WHERE handle IS NOT NULL;

-- Fixed notification set (D14): exactly assigned/mentioned/watched_comment,
-- no delivery-channel column (in-app only), no per-user preferences. Shape
-- matches docs/REDUCED_SCOPE_DATA_MODEL.md: user_id, type, issue_id
-- nullable, read_at.
CREATE TABLE IF NOT EXISTS notifications (
    id VARCHAR(36) PRIMARY KEY,
    user_id VARCHAR(36) NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    type VARCHAR(20) NOT NULL CHECK (type IN ('assigned', 'mentioned', 'watched_comment')),
    issue_id VARCHAR(36) REFERENCES issues(id) ON DELETE CASCADE,
    read_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_notifications_user_unread ON notifications(user_id, read_at);
