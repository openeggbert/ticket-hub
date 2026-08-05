-- Custom fields (D9, deferred-after-V1, user-requested): admin-defined
-- fields scoped to a single project, shown on ticket create/edit/view.
-- Deliberately scoped down from D9's full target ("fields support
-- project/issue-type contexts, ordering, required/hidden settings,
-- defaults, and show-on-create/edit/view flags"): one context per field
-- (its project, not also its issue type -- D9 already rules out named
-- screens/screen schemes, and per-issue-type contexts on top of that
-- would multiply this feature's surface well past a single batch), a
-- fixed always-shown-everywhere visibility (no per-stage hidden/show-on
-- flags), and no per-field default value. `sort_order` is kept (D9 calls
-- for ordering) using the exact same "small integer column, renumbered by
-- the application layer" approach as ticket manual ordering (D31)
-- already established, rather than a fractional/string rank.
--
-- A field's stored/wire value is always a single string, even for
-- multi_select (comma-joined) -- the same convention `labels` already
-- uses elsewhere in this app, chosen to avoid a second "value is
-- sometimes an array" JSON shape that only custom fields would need.
-- Values live in a separate join table (ticket_custom_field_values,
-- mirroring the ticket_labels join-table pattern) rather than being
-- embedded into the main tickets row/SELECT the way labels/component are
-- -- a project's custom fields are dynamic and per-project, not a small
-- fixed global catalog, so folding them into the already-complex ticket
-- read query would be high-risk for comparatively little benefit; the web
-- client fetches a ticket's custom field values with their own request,
-- exactly like it already does for comments/worklogs/links/watchers/
-- voters/attachments/history.

CREATE TABLE IF NOT EXISTS custom_fields (
    id TEXT PRIMARY KEY,
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    field_type TEXT NOT NULL CHECK (field_type IN ('text', 'number', 'date', 'checkbox', 'single_select', 'multi_select')),
    -- JSON array of option strings; only meaningful for single_select/multi_select.
    options TEXT NOT NULL DEFAULT '[]',
    required INTEGER NOT NULL DEFAULT 0,
    sort_order INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (project_id, name)
);

CREATE INDEX IF NOT EXISTS idx_custom_fields_project ON custom_fields(project_id, sort_order);

CREATE TABLE IF NOT EXISTS ticket_custom_field_values (
    ticket_id TEXT NOT NULL REFERENCES tickets(id) ON DELETE CASCADE,
    field_id TEXT NOT NULL REFERENCES custom_fields(id) ON DELETE CASCADE,
    value TEXT NOT NULL,
    PRIMARY KEY (ticket_id, field_id)
);

CREATE INDEX IF NOT EXISTS idx_ticket_custom_field_values_field ON ticket_custom_field_values(field_id);
