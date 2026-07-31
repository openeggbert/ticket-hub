-- Simplified worklogs (Phase 4, D12/D13): time spent + an optional comment
-- only -- no remaining-estimate linkage (D12 dropped time estimates
-- entirely) and no separate own-vs-others edit/delete permission split
-- (D13: any project member with issue access may edit or delete any
-- worklog on that issue, not just their own). Tombstone delete mirrors the
-- comments/issues/projects pattern (deleted_at/deleted_by_user_id).

CREATE TABLE IF NOT EXISTS worklogs (
    id VARCHAR(36) PRIMARY KEY,
    issue_id VARCHAR(36) NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    author_user_id VARCHAR(36) NOT NULL REFERENCES users(id),
    work_date DATE NOT NULL,
    time_spent_seconds INTEGER NOT NULL CHECK (time_spent_seconds > 0),
    comment TEXT,
    deleted_at TIMESTAMPTZ,
    deleted_by_user_id VARCHAR(36) REFERENCES users(id),
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    version INTEGER NOT NULL DEFAULT 1
);

CREATE INDEX IF NOT EXISTS idx_worklogs_issue ON worklogs(issue_id, work_date);
