CREATE TABLE IF NOT EXISTS schema_migrations (
    version VARCHAR(128) PRIMARY KEY,
    checksum VARCHAR(64),
    applied_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS users (
    id VARCHAR(36) PRIMARY KEY,
    username VARCHAR(80) NOT NULL UNIQUE,
    display_name VARCHAR(160) NOT NULL,
    email VARCHAR(320) NOT NULL UNIQUE,
    avatar_url TEXT,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS projects (
    id VARCHAR(36) PRIMARY KEY,
    project_key VARCHAR(12) NOT NULL UNIQUE CHECK (project_key ~ '^[A-Z][A-Z0-9]{1,11}$'),
    name VARCHAR(160) NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    lead_user_id VARCHAR(36) REFERENCES users(id) ON DELETE SET NULL,
    next_issue_number BIGINT NOT NULL DEFAULT 1 CHECK (next_issue_number > 0),
    archived BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS project_members (
    project_id VARCHAR(36) NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    user_id VARCHAR(36) NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role_key VARCHAR(40) NOT NULL DEFAULT 'member',
    joined_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (project_id, user_id)
);

CREATE TABLE IF NOT EXISTS issue_types (
    id VARCHAR(36) PRIMARY KEY,
    type_key VARCHAR(40) NOT NULL UNIQUE,
    name VARCHAR(80) NOT NULL,
    icon VARCHAR(40) NOT NULL,
    color VARCHAR(20) NOT NULL,
    hierarchy_level INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS issue_statuses (
    id VARCHAR(36) PRIMARY KEY,
    status_key VARCHAR(40) NOT NULL UNIQUE,
    name VARCHAR(80) NOT NULL,
    category VARCHAR(20) NOT NULL CHECK (category IN ('todo', 'in_progress', 'done')),
    sort_order INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS priorities (
    id VARCHAR(36) PRIMARY KEY,
    priority_key VARCHAR(40) NOT NULL UNIQUE,
    name VARCHAR(80) NOT NULL,
    rank INTEGER NOT NULL,
    color VARCHAR(20) NOT NULL
);

CREATE TABLE IF NOT EXISTS issues (
    id VARCHAR(36) PRIMARY KEY,
    project_id VARCHAR(36) NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    issue_number BIGINT NOT NULL CHECK (issue_number > 0),
    issue_key VARCHAR(64) NOT NULL UNIQUE CHECK (issue_key = UPPER(issue_key)),
    summary VARCHAR(255) NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    issue_type_id VARCHAR(36) NOT NULL REFERENCES issue_types(id),
    status_id VARCHAR(36) NOT NULL REFERENCES issue_statuses(id),
    priority_id VARCHAR(36) NOT NULL REFERENCES priorities(id),
    reporter_user_id VARCHAR(36) NOT NULL REFERENCES users(id),
    assignee_user_id VARCHAR(36) REFERENCES users(id) ON DELETE SET NULL,
    parent_issue_id VARCHAR(36) REFERENCES issues(id) ON DELETE SET NULL,
    story_points DOUBLE PRECISION CHECK (story_points IS NULL OR story_points >= 0),
    due_date DATE,
    resolution VARCHAR(80) CHECK (resolution IS NULL OR resolution IN ('fixed', 'done', 'wont-fix', 'duplicate', 'cannot-reproduce')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (project_id, issue_number)
);

CREATE INDEX IF NOT EXISTS idx_issues_project ON issues(project_id);
CREATE INDEX IF NOT EXISTS idx_issues_status ON issues(status_id);
CREATE INDEX IF NOT EXISTS idx_issues_assignee ON issues(assignee_user_id);
CREATE INDEX IF NOT EXISTS idx_issues_updated ON issues(updated_at DESC);

CREATE TABLE IF NOT EXISTS labels (
    id VARCHAR(36) PRIMARY KEY,
    name VARCHAR(64) NOT NULL UNIQUE,
    color VARCHAR(20) NOT NULL DEFAULT '#6B778C'
);

CREATE TABLE IF NOT EXISTS issue_labels (
    issue_id VARCHAR(36) NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    label_id VARCHAR(36) NOT NULL REFERENCES labels(id) ON DELETE CASCADE,
    PRIMARY KEY (issue_id, label_id)
);

CREATE TABLE IF NOT EXISTS comments (
    id VARCHAR(36) PRIMARY KEY,
    issue_id VARCHAR(36) NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    author_user_id VARCHAR(36) NOT NULL REFERENCES users(id),
    body TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_comments_issue_created ON comments(issue_id, created_at);

CREATE TABLE IF NOT EXISTS issue_history (
    id VARCHAR(36) PRIMARY KEY,
    issue_id VARCHAR(36) NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    actor_user_id VARCHAR(36) REFERENCES users(id) ON DELETE SET NULL,
    field_name VARCHAR(80) NOT NULL,
    old_value TEXT,
    new_value TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS issue_links (
    id VARCHAR(36) PRIMARY KEY,
    source_issue_id VARCHAR(36) NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    target_issue_id VARCHAR(36) NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    link_type VARCHAR(40) NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CHECK (source_issue_id <> target_issue_id)
);

CREATE TABLE IF NOT EXISTS attachments (
    id VARCHAR(36) PRIMARY KEY,
    issue_id VARCHAR(36) NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    uploader_user_id VARCHAR(36) NOT NULL REFERENCES users(id),
    file_name VARCHAR(255) NOT NULL,
    content_type VARCHAR(160) NOT NULL,
    byte_size BIGINT NOT NULL CHECK (byte_size >= 0),
    storage_key TEXT NOT NULL UNIQUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

