-- Kanban board WIP limits (Phase 5, D32/D33). D32 keeps "one board column
-- equals one workflow status" (no per-project boards, no board_id, no
-- multi-status columns): this is a single flat table with one row per
-- fixed workflow status, not one row per project per status. wip_limit is
-- a nullable integer -- a soft, visually-highlighted-only limit (D33's own
-- "cheap: one numeric field per column plus a display-time comparison, no
-- blocking logic"), never enforced server-side. sort_order mirrors
-- issue_statuses.sort_order at seed time; it is duplicated here (rather
-- than always joining) only because the reduced-scope data model
-- (docs/REDUCED_SCOPE_DATA_MODEL.md) lists it as its own column.

CREATE TABLE IF NOT EXISTS board_columns (
    id VARCHAR(36) PRIMARY KEY,
    status_id VARCHAR(36) NOT NULL UNIQUE REFERENCES issue_statuses(id) ON DELETE CASCADE,
    wip_limit INTEGER,
    sort_order INTEGER NOT NULL DEFAULT 0
);
