ALTER TABLE projects ADD COLUMN archived_at TEXT;
ALTER TABLE projects ADD COLUMN deleted_at TEXT;
ALTER TABLE projects ADD COLUMN deleted_by_user_id TEXT REFERENCES users(id) ON DELETE SET NULL;

ALTER TABLE issues ADD COLUMN version INTEGER NOT NULL DEFAULT 1 CHECK (version > 0);
ALTER TABLE issues ADD COLUMN rank_value TEXT NOT NULL DEFAULT 'm';
ALTER TABLE issues ADD COLUMN deleted_at TEXT;
ALTER TABLE issues ADD COLUMN deleted_by_user_id TEXT REFERENCES users(id) ON DELETE SET NULL;

ALTER TABLE comments ADD COLUMN version INTEGER NOT NULL DEFAULT 1 CHECK (version > 0);
ALTER TABLE comments ADD COLUMN deleted_at TEXT;
ALTER TABLE comments ADD COLUMN deleted_by_user_id TEXT REFERENCES users(id) ON DELETE SET NULL;

ALTER TABLE attachments ADD COLUMN sha256 TEXT;
ALTER TABLE attachments ADD COLUMN deleted_at TEXT;
ALTER TABLE attachments ADD COLUMN deleted_by_user_id TEXT REFERENCES users(id) ON DELETE SET NULL;

CREATE TABLE project_key_aliases (
    alias_key TEXT PRIMARY KEY,
    project_id TEXT REFERENCES projects(id) ON DELETE SET NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE issue_key_aliases (
    alias_key TEXT PRIMARY KEY,
    issue_id TEXT REFERENCES issues(id) ON DELETE SET NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_issue_alias_issue ON issue_key_aliases(issue_id);
CREATE INDEX idx_project_alias_project ON project_key_aliases(project_id);
CREATE INDEX idx_issues_live_updated ON issues(deleted_at, updated_at DESC);
CREATE INDEX idx_comments_live_issue ON comments(issue_id, deleted_at, created_at);
