-- Rename "issue" to "ticket" throughout the schema (user-requested,
-- full-stack terminology rename to match the product's own name "Ticket
-- Hub"). Pure rename -- no data changes, no new columns, no behavior
-- change. SQLite (3.25+) automatically rewrites CHECK constraints and
-- other tables' FOREIGN KEY definitions that reference a renamed table or
-- column; verified empirically before writing this migration. The
-- migration runner's existing PRAGMA foreign_keys OFF/ON + foreign_key_check
-- wrapper (see SqliteDatabase::migrate) is the same safety net every other
-- ALTER-heavy migration in this project already relies on.

ALTER TABLE issues RENAME TO tickets;
ALTER TABLE issue_history RENAME TO ticket_history;
ALTER TABLE issue_key_aliases RENAME TO ticket_key_aliases;
ALTER TABLE issue_labels RENAME TO ticket_labels;
ALTER TABLE issue_links RENAME TO ticket_links;
ALTER TABLE issue_statuses RENAME TO ticket_statuses;
ALTER TABLE issue_types RENAME TO ticket_types;
ALTER TABLE issue_votes RENAME TO ticket_votes;
ALTER TABLE issue_watchers RENAME TO ticket_watchers;

ALTER TABLE tickets RENAME COLUMN issue_number TO ticket_number;
ALTER TABLE tickets RENAME COLUMN issue_key TO ticket_key;
ALTER TABLE tickets RENAME COLUMN issue_type_id TO ticket_type_id;
ALTER TABLE tickets RENAME COLUMN parent_issue_id TO parent_ticket_id;

ALTER TABLE attachments RENAME COLUMN issue_id TO ticket_id;
ALTER TABLE comments RENAME COLUMN issue_id TO ticket_id;
ALTER TABLE ticket_history RENAME COLUMN issue_id TO ticket_id;
ALTER TABLE ticket_key_aliases RENAME COLUMN issue_id TO ticket_id;
ALTER TABLE ticket_labels RENAME COLUMN issue_id TO ticket_id;
ALTER TABLE ticket_links RENAME COLUMN source_issue_id TO source_ticket_id;
ALTER TABLE ticket_links RENAME COLUMN target_issue_id TO target_ticket_id;
ALTER TABLE ticket_votes RENAME COLUMN issue_id TO ticket_id;
ALTER TABLE ticket_watchers RENAME COLUMN issue_id TO ticket_id;
ALTER TABLE notifications RENAME COLUMN issue_id TO ticket_id;
ALTER TABLE projects RENAME COLUMN next_issue_number TO next_ticket_number;
ALTER TABLE worklogs RENAME COLUMN issue_id TO ticket_id;

-- Index names are never referenced by application code (which only ever
-- names tables/columns), but are renamed too for full consistency. SQLite
-- has no ALTER INDEX RENAME, so each is dropped and recreated identically
-- apart from its name -- the column renames above already happened, so
-- these definitions reference the new column names directly.
DROP INDEX IF EXISTS idx_issues_project;
CREATE INDEX IF NOT EXISTS idx_tickets_project ON tickets(project_id);
DROP INDEX IF EXISTS idx_issues_status;
CREATE INDEX IF NOT EXISTS idx_tickets_status ON tickets(status_id);
DROP INDEX IF EXISTS idx_issues_assignee;
CREATE INDEX IF NOT EXISTS idx_tickets_assignee ON tickets(assignee_user_id);
DROP INDEX IF EXISTS idx_issues_updated;
CREATE INDEX IF NOT EXISTS idx_tickets_updated ON tickets(updated_at DESC);
DROP INDEX IF EXISTS idx_issues_live_updated;
CREATE INDEX IF NOT EXISTS idx_tickets_live_updated ON tickets(deleted_at, updated_at DESC);
DROP INDEX IF EXISTS idx_comments_issue_created;
CREATE INDEX IF NOT EXISTS idx_comments_ticket_created ON comments(ticket_id, created_at);
DROP INDEX IF EXISTS idx_comments_live_issue;
CREATE INDEX IF NOT EXISTS idx_comments_live_ticket ON comments(ticket_id, deleted_at, created_at);
DROP INDEX IF EXISTS idx_issue_alias_issue;
CREATE INDEX IF NOT EXISTS idx_ticket_alias_ticket ON ticket_key_aliases(ticket_id);
DROP INDEX IF EXISTS idx_worklogs_issue;
CREATE INDEX IF NOT EXISTS idx_worklogs_ticket ON worklogs(ticket_id);
DROP INDEX IF EXISTS idx_attachments_issue;
CREATE INDEX IF NOT EXISTS idx_attachments_ticket ON attachments(ticket_id);
