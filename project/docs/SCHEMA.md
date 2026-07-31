# Current implemented schema

This document describes the schema physically present in the current prototype migrations. The current
target catalog is [REDUCED_SCOPE_DATA_MODEL.md](REDUCED_SCOPE_DATA_MODEL.md) (the original, larger
[DATA_MODEL.md](DATA_MODEL.md) is long-term reference only).

## Migration execution

Schema files are discovered from `migrations/<backend>/` in filename order. Files containing `_seed_` are excluded from schema migration execution. Applied versions and stable content checksums are stored in `schema_migrations`; modifying an already applied migration causes startup failure. PostgreSQL serializes migration execution with an advisory lock. The SQLite runner additionally disables and re-verifies foreign keys (`PRAGMA foreign_keys` + `PRAGMA foreign_key_check`) around every migration, since SQLite's `ALTER TABLE DROP COLUMN` refuses to drop a column that participates in a `UNIQUE` constraint and some migrations (e.g. `004_identity.sql`) need to rebuild the table instead.

Current schema migrations:

- `001_initial.sql` — MVP entities.
- `003_product_foundation.sql` — versioning, recycle-bin foundations and permanent key aliases.
- `004_identity.sql` — local accounts, sessions, minimal login-attempt lockout (Phase 1 of
  `REDUCED_SCOPE_ROADMAP.md`). Drops `users.username`; adds `users.time_zone`/`clock_format`/`is_admin`,
  plus the new `local_credentials` and `sessions` tables.

`002_seed_demo.sql` remains an explicitly invoked, idempotent development seed rather than a schema migration. It now also inserts a dev-only Argon2id password hash (`demo12345`) into `local_credentials` for all three demo users.

## Current tables

### `schema_migrations`

`version` PK, `checksum`, `applied_at`.

### `users`

`id`, `display_name`, `email` (unique), `avatar_url`, `active`, `time_zone`, `clock_format`, `is_admin`, `created_at`, `updated_at`.

The prototype's temporary `username` column was removed in `004_identity.sql`. There is no `handle` column yet — it is added in a later phase together with @mentions, the first feature that actually needs one (`docs/REMOVED_AND_DEFERRED_FEATURES.md`).

### `local_credentials`

`user_id` PK/FK to `users(id)`, `password_hash` (Argon2id-encoded), `failed_login_count`, `locked_until` nullable, `created_at`, `updated_at`.

Minimal login-attempt lockout only (locks for 15 minutes after `IDatabase::MaxFailedLoginAttempts` consecutive failures). The full configurable rate-limiting policy is a Phase 6 addition (`REDUCED_SCOPE_ROADMAP.md`).

### `sessions`

`id`, `user_id` FK to `users(id)`, `token_hash` (SHA-256 hex, unique), `created_at`, `expires_at`.

The raw session token is never stored, only its SHA-256 hash; it is returned to the caller exactly once, at login. 30-day fixed lifetime; there is no active-session list or "sign out everywhere" endpoint yet (resequenced to Phase 6).

### `projects`

`id`, `project_key`, `name`, `description`, `lead_user_id`, `next_issue_number`, `archived`, `created_at`, `updated_at`, `archived_at`, `deleted_at`, `deleted_by_user_id`.

The application allocates issue numbers transactionally. PostgreSQL locks the project row; SQLite uses `BEGIN IMMEDIATE` and an adapter mutex.

### `project_key_aliases`

`alias_key` PK, `project_id` nullable, `created_at`. A null project ID can preserve key reservation after a future permanent deletion.

### `project_members`

`project_id`, `user_id`, `role_key`, `joined_at`; composite PK.

### Fixed/reference data

- `issue_types`: key, name, icon, color, hierarchy level.
- `issue_statuses`: key, name, one of `todo`, `in_progress`, `done`, sort order.
- `priorities`: fixed key/name/rank/color rows.

The demo seed includes Epic, Story, Task, Bug and Sub-task, but full hierarchy enforcement is not yet implemented.

### `issues`

Core columns:

- identity: `id`, `project_id`, `issue_number`, `issue_key`,
- content: `summary`, `description`,
- classification: `issue_type_id`, `status_id`, `priority_id`, `resolution`,
- people: `reporter_user_id`, `assignee_user_id`,
- hierarchy: `parent_issue_id`,
- planning: `story_points`, `due_date`, `rank_value`,
- lifecycle: `created_at`, `updated_at`, `version`, `deleted_at`, `deleted_by_user_id`.

Ordinary list, detail and dashboard queries exclude deleted issues. Status changes increment `version`; an expected stale version raises a concurrency conflict.

### `issue_key_aliases`

`alias_key` PK, `issue_id` nullable, `created_at`. Detail and comment operations resolve both current and alias keys.

### Labels

- `labels`: `id`, globally normalized `name`, color.
- `issue_labels`: issue/label composite PK.

Application services trim, lowercase and deduplicate new labels.

### Comments

`id`, `issue_id`, `author_user_id`, `body`, `created_at`, `updated_at`, `version`, `deleted_at`, `deleted_by_user_id`.

Ordinary comment lists exclude deleted comments. Version-history and tombstone APIs are planned but not yet implemented.

### `issue_history`

`id`, `issue_id`, `actor_user_id`, `field_name`, `old_value`, `new_value`, `created_at`.

The prototype currently records status changes. The target replaces display-oriented strings with fully typed structured history.

### `issue_links`

`id`, source issue, target issue, link type string, created time. A configurable link-type catalog is not yet implemented.

### `attachments`

`id`, issue, uploader, filename, content type, byte size, storage key, created time, SHA-256 nullable, deletion fields.

Only metadata exists. Filesystem/S3 providers and attachment APIs remain future phases.

## Current indexes

Indexes cover project/status/assignee/update issue access, live issue listing, comment timelines, aliases, label joins, and session lookup/expiry. Full-text indexes are not planned at all for V1 -- search uses a plain `LIKE`/`ILIKE` query (`docs/REDUCED_SCOPE_SPECIFICATION.md` section 10).

## Deliberate implementation gap

The current schema is a migration-safe foundation for the **reduced-scope V1**, not the full original
target model. Fixed project roles, the fixed workflow, boards, attachments, and the REST API surface are
defined in `REDUCED_SCOPE_DATA_MODEL.md` and introduced only in their roadmap phases
(`REDUCED_SCOPE_ROADMAP.md`). Permission schemes, workflow versions/drafts, custom fields, saved filters,
sprints, notifications schemes, jobs, event log, and webhooks are not part of the V1 plan at all -- see
`docs/REMOVED_AND_DEFERRED_FEATURES.md`.
