-- Comment edited-flag (Phase 4, D81): an edited timestamp instead of a
-- separate comment_versions/history table -- the simplified V1 answer keeps
-- only "this comment was edited at X", not the prior text.
ALTER TABLE comments ADD COLUMN edited_at TEXT;
