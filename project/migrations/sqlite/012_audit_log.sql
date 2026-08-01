-- Simple append-only admin/security audit log (Phase 4, D23): same shape
-- as the long-term design minus category-based retention -- rows are
-- never auto-purged, and there is no separate retention-policy table.
-- actor_user_id is nullable: the only account-creation path
-- (ticket-hub-cli create-user) runs outside any web session and has no
-- Principal to attribute the event to, and a failed/blocked login attempt
-- has no authenticated actor by definition.

CREATE TABLE IF NOT EXISTS audit_events (
    id TEXT PRIMARY KEY,
    category TEXT NOT NULL,
    action TEXT NOT NULL,
    actor_user_id TEXT REFERENCES users(id) ON DELETE SET NULL,
    target_type TEXT,
    target_id TEXT,
    details TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_audit_events_created ON audit_events(created_at);
