-- @handle mentions (D56/D80) and the fixed in-app notification set (D14).
-- SQLite's ALTER TABLE ADD COLUMN cannot carry a UNIQUE constraint, so the
-- uniqueness is a separate partial index instead (NULL handles never
-- collide, matching the column's optional nature).

ALTER TABLE users ADD COLUMN handle TEXT;
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_handle ON users(handle) WHERE handle IS NOT NULL;

-- Fixed notification set (D14): exactly assigned/mentioned/watched_comment,
-- no delivery-channel column (in-app only), no per-user preferences. Shape
-- matches docs/REDUCED_SCOPE_DATA_MODEL.md: user_id, type, issue_id
-- nullable, read_at.
CREATE TABLE IF NOT EXISTS notifications (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    type TEXT NOT NULL CHECK (type IN ('assigned', 'mentioned', 'watched_comment')),
    issue_id TEXT REFERENCES issues(id) ON DELETE CASCADE,
    read_at TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_notifications_user_unread ON notifications(user_id, read_at);
