-- Phase 2 (reduced scope): fixed project roles (already representable via
-- the existing project_members.role_key column -- no new table needed, see
-- docs/REDUCED_SCOPE_DATA_MODEL.md section C) plus the tiny generic settings
-- store used for the anonymous-read-access toggle (D59) and nothing else.
-- No permission_schemes/permission_grants tables -- those were replaced
-- entirely by the fixed role model in D3.

CREATE TABLE IF NOT EXISTS installation_settings (
    setting_key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
