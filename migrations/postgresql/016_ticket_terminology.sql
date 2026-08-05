-- Rename "issue" to "ticket" throughout the schema (user-requested,
-- full-stack terminology rename to match the product's own name "Ticket
-- Hub"). Pure rename -- no data changes, no new columns, no behavior
-- change. PostgreSQL tracks dependencies by object id, not by name, so
-- renaming a table or column automatically and safely rewrites every
-- referencing FOREIGN KEY/CHECK constraint, index, and sequence default in
-- other tables -- no equivalent of SQLite's foreign-key-toggle dance is
-- needed here.

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

ALTER INDEX idx_issues_project RENAME TO idx_tickets_project;
ALTER INDEX idx_issues_status RENAME TO idx_tickets_status;
ALTER INDEX idx_issues_assignee RENAME TO idx_tickets_assignee;
ALTER INDEX idx_issues_updated RENAME TO idx_tickets_updated;
ALTER INDEX idx_issues_live_updated RENAME TO idx_tickets_live_updated;
ALTER INDEX idx_comments_issue_created RENAME TO idx_comments_ticket_created;
ALTER INDEX idx_comments_live_issue RENAME TO idx_comments_live_ticket;
ALTER INDEX idx_issue_alias_issue RENAME TO idx_ticket_alias_ticket;
ALTER INDEX idx_worklogs_issue RENAME TO idx_worklogs_ticket;
ALTER INDEX idx_attachments_issue RENAME TO idx_attachments_ticket;

-- Table/column RENAME does not rename the constraints Postgres auto-named
-- after the original table/column at creation time (primary keys, unique
-- constraints, foreign keys, check constraints) -- renamed explicitly here
-- for full consistency; none of these names are referenced by application
-- code, this is cosmetic-only and carries zero behavior change.
ALTER TABLE attachments RENAME CONSTRAINT attachments_issue_id_fkey TO attachments_ticket_id_fkey;
ALTER TABLE comments RENAME CONSTRAINT comments_issue_id_fkey TO comments_ticket_id_fkey;
ALTER TABLE notifications RENAME CONSTRAINT notifications_issue_id_fkey TO notifications_ticket_id_fkey;
ALTER TABLE projects RENAME CONSTRAINT projects_next_issue_number_check TO projects_next_ticket_number_check;
ALTER TABLE ticket_history RENAME CONSTRAINT issue_history_actor_user_id_fkey TO ticket_history_actor_user_id_fkey;
ALTER TABLE ticket_history RENAME CONSTRAINT issue_history_issue_id_fkey TO ticket_history_ticket_id_fkey;
ALTER TABLE ticket_history RENAME CONSTRAINT issue_history_pkey TO ticket_history_pkey;
ALTER TABLE ticket_key_aliases RENAME CONSTRAINT issue_key_aliases_issue_id_fkey TO ticket_key_aliases_ticket_id_fkey;
ALTER TABLE ticket_key_aliases RENAME CONSTRAINT issue_key_aliases_pkey TO ticket_key_aliases_pkey;
ALTER TABLE ticket_labels RENAME CONSTRAINT issue_labels_issue_id_fkey TO ticket_labels_ticket_id_fkey;
ALTER TABLE ticket_labels RENAME CONSTRAINT issue_labels_label_id_fkey TO ticket_labels_label_id_fkey;
ALTER TABLE ticket_labels RENAME CONSTRAINT issue_labels_pkey TO ticket_labels_pkey;
ALTER TABLE ticket_links RENAME CONSTRAINT issue_links_check TO ticket_links_check;
ALTER TABLE ticket_links RENAME CONSTRAINT issue_links_pkey TO ticket_links_pkey;
ALTER TABLE ticket_links RENAME CONSTRAINT issue_links_source_issue_id_fkey TO ticket_links_source_ticket_id_fkey;
ALTER TABLE ticket_links RENAME CONSTRAINT issue_links_target_issue_id_fkey TO ticket_links_target_ticket_id_fkey;
ALTER TABLE ticket_statuses RENAME CONSTRAINT issue_statuses_category_check TO ticket_statuses_category_check;
ALTER TABLE ticket_statuses RENAME CONSTRAINT issue_statuses_pkey TO ticket_statuses_pkey;
ALTER TABLE ticket_statuses RENAME CONSTRAINT issue_statuses_status_key_key TO ticket_statuses_status_key_key;
ALTER TABLE ticket_types RENAME CONSTRAINT issue_types_pkey TO ticket_types_pkey;
ALTER TABLE ticket_types RENAME CONSTRAINT issue_types_type_key_key TO ticket_types_type_key_key;
ALTER TABLE ticket_votes RENAME CONSTRAINT issue_votes_issue_id_fkey TO ticket_votes_ticket_id_fkey;
ALTER TABLE ticket_votes RENAME CONSTRAINT issue_votes_pkey TO ticket_votes_pkey;
ALTER TABLE ticket_votes RENAME CONSTRAINT issue_votes_user_id_fkey TO ticket_votes_user_id_fkey;
ALTER TABLE ticket_watchers RENAME CONSTRAINT issue_watchers_issue_id_fkey TO ticket_watchers_ticket_id_fkey;
ALTER TABLE ticket_watchers RENAME CONSTRAINT issue_watchers_pkey TO ticket_watchers_pkey;
ALTER TABLE ticket_watchers RENAME CONSTRAINT issue_watchers_user_id_fkey TO ticket_watchers_user_id_fkey;
ALTER TABLE tickets RENAME CONSTRAINT issues_assignee_user_id_fkey TO tickets_assignee_user_id_fkey;
ALTER TABLE tickets RENAME CONSTRAINT issues_deleted_by_user_id_fkey TO tickets_deleted_by_user_id_fkey;
ALTER TABLE tickets RENAME CONSTRAINT issues_issue_key_check TO tickets_ticket_key_check;
ALTER TABLE tickets RENAME CONSTRAINT issues_issue_key_key TO tickets_ticket_key_key;
ALTER TABLE tickets RENAME CONSTRAINT issues_issue_number_check TO tickets_ticket_number_check;
ALTER TABLE tickets RENAME CONSTRAINT issues_issue_type_id_fkey TO tickets_ticket_type_id_fkey;
ALTER TABLE tickets RENAME CONSTRAINT issues_parent_issue_id_fkey TO tickets_parent_ticket_id_fkey;
ALTER TABLE tickets RENAME CONSTRAINT issues_pkey TO tickets_pkey;
ALTER TABLE tickets RENAME CONSTRAINT issues_priority_id_fkey TO tickets_priority_id_fkey;
ALTER TABLE tickets RENAME CONSTRAINT issues_project_id_fkey TO tickets_project_id_fkey;
ALTER TABLE tickets RENAME CONSTRAINT issues_project_id_issue_number_key TO tickets_project_id_ticket_number_key;
ALTER TABLE tickets RENAME CONSTRAINT issues_reporter_user_id_fkey TO tickets_reporter_user_id_fkey;
ALTER TABLE tickets RENAME CONSTRAINT issues_resolution_check TO tickets_resolution_check;
ALTER TABLE tickets RENAME CONSTRAINT issues_status_id_fkey TO tickets_status_id_fkey;
ALTER TABLE tickets RENAME CONSTRAINT issues_story_points_check TO tickets_story_points_check;
ALTER TABLE tickets RENAME CONSTRAINT issues_version_check TO tickets_version_check;
ALTER TABLE worklogs RENAME CONSTRAINT worklogs_issue_id_fkey TO worklogs_ticket_id_fkey;
