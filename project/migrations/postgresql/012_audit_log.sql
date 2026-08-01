-- Simple append-only admin/security audit log (Phase 4, D23): same shape
-- as the long-term design minus category-based retention -- rows are
-- never auto-purged, and there is no separate retention-policy table.
-- actor_user_id is nullable: the only account-creation path
-- (ticket-hub-cli create-user) runs outside any web session and has no
-- Principal to attribute the event to, and a failed/blocked login attempt
-- has no authenticated actor by definition.

CREATE TABLE IF NOT EXISTS audit_events (
    id VARCHAR(36) PRIMARY KEY,
    category VARCHAR(40) NOT NULL,
    action VARCHAR(120) NOT NULL,
    actor_user_id VARCHAR(36) REFERENCES users(id) ON DELETE SET NULL,
    target_type VARCHAR(40),
    target_id VARCHAR(200),
    details TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_audit_events_created ON audit_events(created_at);
