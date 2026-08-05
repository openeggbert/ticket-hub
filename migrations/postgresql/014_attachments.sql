-- Attachments (Phase 5, D15/D98-D105). `attachments.sha256`/`deleted_at`/
-- `deleted_by_user_id` already exist from `003_product_foundation.sql`
-- (pre-provisioned ahead of this phase); this migration only adds the
-- issue_id index that table never got. D101/D102: the tombstone
-- (deleted_at/deleted_by_user_id) recycle-bin pattern already used for
-- issues/comments/projects/worklogs, fixed 90-day on-demand retention (no
-- background purge job). D105: upload-time SHA-256/size verification only,
-- no periodic background integrity audit -- sha256 is recorded once, at
-- upload, and never re-checked.

CREATE INDEX IF NOT EXISTS idx_attachments_issue ON attachments(issue_id);
