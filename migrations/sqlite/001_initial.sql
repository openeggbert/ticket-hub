CREATE TABLE IF NOT EXISTS schema_migrations (
    version TEXT PRIMARY KEY,
    checksum TEXT,
    applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,
    username TEXT NOT NULL UNIQUE,
    display_name TEXT NOT NULL,
    email TEXT NOT NULL UNIQUE,
    avatar_url TEXT,
    active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0, 1)),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS projects (
    id TEXT PRIMARY KEY,
    project_key TEXT NOT NULL UNIQUE
        CHECK (length(project_key) BETWEEN 2 AND 12
               AND substr(project_key, 1, 1) GLOB '[A-Z]'
               AND project_key NOT GLOB '*[^A-Z0-9]*'),
    name TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    lead_user_id TEXT REFERENCES users(id) ON DELETE SET NULL,
    next_issue_number INTEGER NOT NULL DEFAULT 1 CHECK (next_issue_number > 0),
    archived INTEGER NOT NULL DEFAULT 0 CHECK (archived IN (0, 1)),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS project_members (
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role_key TEXT NOT NULL DEFAULT 'member',
    joined_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (project_id, user_id)
);

CREATE TABLE IF NOT EXISTS issue_types (
    id TEXT PRIMARY KEY,
    type_key TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    icon TEXT NOT NULL,
    color TEXT NOT NULL,
    hierarchy_level INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS issue_statuses (
    id TEXT PRIMARY KEY,
    status_key TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    category TEXT NOT NULL CHECK (category IN ('todo', 'in_progress', 'done')),
    sort_order INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS priorities (
    id TEXT PRIMARY KEY,
    priority_key TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    rank INTEGER NOT NULL,
    color TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS issues (
    id TEXT PRIMARY KEY,
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    issue_number INTEGER NOT NULL CHECK (issue_number > 0),
    issue_key TEXT NOT NULL UNIQUE CHECK (issue_key = UPPER(issue_key)),
    summary TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    issue_type_id TEXT NOT NULL REFERENCES issue_types(id),
    status_id TEXT NOT NULL REFERENCES issue_statuses(id),
    priority_id TEXT NOT NULL REFERENCES priorities(id),
    reporter_user_id TEXT NOT NULL REFERENCES users(id),
    assignee_user_id TEXT REFERENCES users(id) ON DELETE SET NULL,
    parent_issue_id TEXT REFERENCES issues(id) ON DELETE SET NULL,
    story_points REAL CHECK (story_points IS NULL OR story_points >= 0),
    due_date TEXT,
    resolution TEXT CHECK (resolution IS NULL OR resolution IN ('fixed', 'done', 'wont-fix', 'duplicate', 'cannot-reproduce')),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (project_id, issue_number)
);

CREATE INDEX IF NOT EXISTS idx_issues_project ON issues(project_id);
CREATE INDEX IF NOT EXISTS idx_issues_status ON issues(status_id);
CREATE INDEX IF NOT EXISTS idx_issues_assignee ON issues(assignee_user_id);
CREATE INDEX IF NOT EXISTS idx_issues_updated ON issues(updated_at DESC);

CREATE TABLE IF NOT EXISTS labels (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    color TEXT NOT NULL DEFAULT '#6B778C'
);

CREATE TABLE IF NOT EXISTS issue_labels (
    issue_id TEXT NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    label_id TEXT NOT NULL REFERENCES labels(id) ON DELETE CASCADE,
    PRIMARY KEY (issue_id, label_id)
);

CREATE TABLE IF NOT EXISTS comments (
    id TEXT PRIMARY KEY,
    issue_id TEXT NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    author_user_id TEXT NOT NULL REFERENCES users(id),
    body TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_comments_issue_created ON comments(issue_id, created_at);

CREATE TABLE IF NOT EXISTS issue_history (
    id TEXT PRIMARY KEY,
    issue_id TEXT NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    actor_user_id TEXT REFERENCES users(id) ON DELETE SET NULL,
    field_name TEXT NOT NULL,
    old_value TEXT,
    new_value TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS issue_links (
    id TEXT PRIMARY KEY,
    source_issue_id TEXT NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    target_issue_id TEXT NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    link_type TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CHECK (source_issue_id <> target_issue_id)
);

CREATE TABLE IF NOT EXISTS attachments (
    id TEXT PRIMARY KEY,
    issue_id TEXT NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    uploader_user_id TEXT NOT NULL REFERENCES users(id),
    file_name TEXT NOT NULL,
    content_type TEXT NOT NULL,
    byte_size INTEGER NOT NULL CHECK (byte_size >= 0),
    storage_key TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

