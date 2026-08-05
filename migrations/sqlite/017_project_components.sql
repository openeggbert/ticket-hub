-- Project components (D19, KEEP_FOR_V1): "Simple project components: name,
-- description, lead, default assignee; at most one per issue [ticket]."
-- Already the cheapest reasonable form ("small table plus one optional
-- issue field") -- kept exactly as originally decided, with no scope
-- reduction. No recycle bin/soft-delete: D19 does not call for one (unlike
-- tickets/projects/comments, which have explicit D22/D88-D89/D82
-- decisions), so this stays a plain table; deleting a component clears it
-- from any ticket via ON DELETE SET NULL rather than needing an admin
-- restore path.

CREATE TABLE IF NOT EXISTS project_components (
    id TEXT PRIMARY KEY,
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    lead_user_id TEXT REFERENCES users(id) ON DELETE SET NULL,
    default_assignee_user_id TEXT REFERENCES users(id) ON DELETE SET NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (project_id, name)
);

CREATE INDEX IF NOT EXISTS idx_project_components_project ON project_components(project_id);

ALTER TABLE tickets ADD COLUMN component_id TEXT REFERENCES project_components(id) ON DELETE SET NULL;

CREATE INDEX IF NOT EXISTS idx_tickets_component ON tickets(component_id);
