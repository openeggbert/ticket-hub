# Current implemented schema

This document describes the schema physically present in the current prototype migrations. The approved target catalog is in [DATA_MODEL.md](DATA_MODEL.md).

## Migration execution

Schema files are discovered from `migrations/<backend>/` in filename order. Files containing `_seed_` are excluded from schema migration execution. Applied versions and stable content checksums are stored in `schema_migrations`; modifying an already applied migration causes startup failure. PostgreSQL serializes migration execution with an advisory lock.

Current schema migrations:

- `001_initial.sql` — MVP entities.
- `003_product_foundation.sql` — versioning, recycle-bin foundations and permanent key aliases.

`002_seed_demo.sql` remains an explicitly invoked, idempotent development seed rather than a schema migration.

## Current tables

### `schema_migrations`

`version` PK, `checksum`, `applied_at`.

### `users`

`id`, `username`, `display_name`, `email`, `avatar_url`, `active`, `created_at`, `updated_at`.

`username` is the prototype's temporary login/handle column. The identity phase will migrate it to the approved UUID + unique email + optional handle model.

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

Indexes cover project/status/assignee/update issue access, live issue listing, comment timelines, aliases and label joins. Full-text indexes are not yet present.

## Deliberate implementation gap

The current schema is a migration-safe prototype foundation, not the full target model. Authentication, permission schemes, workflow versions, custom fields, filters, boards, sprints, worklogs, notifications, jobs, event log, webhooks and backup metadata are defined in `DATA_MODEL.md` and introduced only in their roadmap phases.
