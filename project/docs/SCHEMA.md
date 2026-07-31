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
- `005_authorization.sql` — fixed project roles and project lifecycle (Phase 2 of
  `REDUCED_SCOPE_ROADMAP.md`). Adds the `installation_settings` key/value table; no column changes to
  existing tables (`project_members.role_key` and the `archived`/`deleted_at` columns on `projects`
  already existed).
- `006_collaboration.sql` — self-service watchers and voting (Phase 3 of `REDUCED_SCOPE_ROADMAP.md`,
  D20/D79). Adds `issue_watchers` and `issue_votes`.

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

Archiving (D87) and the recycle bin (D88/D89) are now enforced at the application layer, not just schema
columns: `listProjects` excludes both archived and soft-deleted projects from the active list;
`listDeletedProjects` purges anything with `deleted_at` older than 90 days before returning results (no
background job -- purge happens on next access); the project key stays reserved (the `project_key`
column keeps its `UNIQUE` constraint across soft-deleted rows) until `permanentlyDeleteProject` (D90).

### `project_key_aliases`

`alias_key` PK, `project_id` nullable, `created_at`. A null project ID can preserve key reservation after a future permanent deletion.

### `project_members`

`project_id`, `user_id`, `role_key`, `joined_at`; composite PK.

`role_key` is one of the three fixed V1 roles (`Domain::ProjectRoleViewer`/`Member`/`Admin`; D3) -- there
are no configurable permission schemes. A user with no membership row (or an unrecognized `role_key`) has
no access to that project's writes; `Domain::projectRoleRank` returns -1 for both cases so callers cannot
tell them apart. A `users.is_admin` global administrator bypasses this check entirely and is implicitly
Admin on every project. `createProject` inserts the creator as project Admin automatically.

### `installation_settings`

`setting_key` PK, `value`, `updated_at`. A small generic key/value store for the handful of
installation-level toggles that survived scope reduction -- currently just `anonymous_read_access`
(`true`/`false`, absent means disabled), D59. Not a general settings framework
(`REDUCED_SCOPE_DATA_MODEL.md` section A).

### Fixed/reference data

- `issue_types`: key, name, icon, color, hierarchy level.
- `issue_statuses`: key, name, one of `todo`, `in_progress`, `done`, sort order.
- `priorities`: fixed key/name/rank/color rows.

The demo seed includes Epic, Story, Task, Bug and Sub-task. Hierarchy is enforced at the application
layer (Phase 3 of `REDUCED_SCOPE_ROADMAP.md`), not by a database constraint: `issue_types.hierarchy_level`
isn't consulted at all -- `Domain::issueTypeHierarchyLevel` hardcodes the fixed Epic(1)/Story-Task-Bug(0)/
Sub-task(-1) levels (D5, D29, D64-D66), since there are no custom types in V1.

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

`IDatabase::editIssue` (Phase 3, D129) is a full-replacement edit of `summary`/`description`/
`priority_id`/`assignee_user_id`/`story_points`/`due_date`/labels, sharing the same optimistic-locking
contract as `changeIssueStatus`. It does not touch `issue_type_id` or `parent_issue_id` -- re-typing or
re-parenting an issue after creation is not yet implemented.

`TicketService::cloneIssue` (D60, Phase 3) copies `summary`/`description`/`issue_type_id`/`priority_id`/
labels into a new issue in the same project via the existing `createIssue` path (so the new issue gets a
fresh key, hierarchy validation, etc. for free), then records a `clones` link back to the original. It
does **not** copy assignee, story points, due date, attachments, sub-tasks, or other links -- and does
not copy the parent/Epic link either, with one structural exception: cloning a Sub-task keeps its
original `parent_issue_id`, since a Sub-task cannot exist without one (D64) and dropping it would create
an invalid issue, not merely an incomplete copy.

`parent_issue_id` now has application-layer meaning (Phase 3): `TicketService::createIssue` rejects a
request that violates the fixed hierarchy (a Sub-task without a parent, a parent of the wrong type, or a
parent in a different project) with `std::invalid_argument` before the row is ever inserted.

`resolution` (`fixed`/`done`/`wont-fix`/`duplicate`/`cannot-reproduce`, CHECK-constrained since
`001_initial.sql`) is now set and cleared by `changeIssueStatus` itself, transactionally with the status
update (D68-D70): required when the target status's `category` is `done` (missing or unrecognized ->
`Domain::WorkflowViolation`), forced to `NULL` when leaving a `done`-category status ("reopening"), and
left untouched for any other transition. The same transaction also rejects completing an issue
(`Domain::WorkflowViolation`) while any non-deleted child (`parent_issue_id` pointing at it) has a
non-`done`-category status -- the fixed "sub-task completion gate" (D68). Reopening a parent never
touches its children's rows (D69) -- there is no cascade to implement.

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

Records status changes (`field_name = 'status'`) and, since Phase 3's `editIssue`, one row per
standard field that actually changed value on a full-field edit (`field_name` one of `summary`,
`description`, `priority`, `assignee`, `story_points`, `due_date`) -- a field left unchanged writes no
row. The target replaces these display-oriented strings with fully typed structured history.

### `issue_links`

`id`, `source_issue_id`, `target_issue_id`, `link_type`, `created_at`; `CHECK (source_issue_id <>
target_issue_id)`.

The fixed link-type catalog (D17, Phase 3) is enforced at the application layer, not by a database
CHECK/enum: `Domain::isValidLinkType` accepts exactly `blocks`, `relates_to`, `duplicates`, `clones` --
there is no admin-configurable catalog. A link is one directed row but is meaningful from either end;
`IDatabase::listIssueLinks(issueKey)` returns it from both the source's ("outward", label e.g.
`blocks`) and the target's ("inward", label e.g. `is blocked by`) perspective via
`Domain::linkTypeLabels`. `relates_to` uses the same label both ways. `IDatabase::createIssueLink`
rejects an exact-duplicate `(source, target, linkType)` triple; `TicketService::createIssueLink`
additionally requires project-Member-or-above on **both** issues' projects, since a link write touches
two issues that may be in different projects. `TicketService::cloneIssue` (D60) creates one automatically
(`clones`, clone -> original) whenever an issue is cloned.

### `issue_watchers` and `issue_votes`

Both: `issue_id`, `user_id`, `created_at`; composite PK `(issue_id, user_id)`; `ON DELETE CASCADE` on
both foreign keys. Structurally identical -- no other columns, since D20/D79 keep both features to
exactly "who is watching/voting", with no priority-change side effect from votes and no
authorized-user-manages-others-watchers flow. `TicketService::watchIssue`/`voteIssue` and their
`unwatch`/`unvote` counterparts are the entire write surface, and deliberately have **no project-role
check** -- unlike every other issue write, only project-Member-or-above -- since Jira itself gates
watch/vote by "browse" access rather than a write-capable role, and this reduced model's closest
equivalent is simply being an authenticated user (D58). `watchIssue`/`voteIssue` return `true` only when
the row was newly inserted (idempotent on a repeat call); `unwatchIssue`/`unvoteIssue` return `true` only
when a row was actually removed.

### `attachments`

`id`, issue, uploader, filename, content type, byte size, storage key, created time, SHA-256 nullable, deletion fields.

Only metadata exists. Filesystem/S3 providers and attachment APIs remain future phases.

## Current indexes

Indexes cover project/status/assignee/update issue access, live issue listing, comment timelines, aliases, label joins, and session lookup/expiry. Full-text indexes are not planned at all for V1 -- search uses a plain `LIKE`/`ILIKE` query (`docs/REDUCED_SCOPE_SPECIFICATION.md` section 10).

## Deliberate implementation gap

The current schema is a migration-safe foundation for the **reduced-scope V1**, not the full original
target model. Fixed project roles and project lifecycle (Phase 2), and the fixed workflow/hierarchy rules
plus resolution handling (Phase 3, partial -- see `NEXT.md` for exactly what of Phase 3 remains: cloning,
issue links, watchers, voting, bulk actions, and the rank/renumber migration are not yet built) are now
enforced. Boards and attachments remain future phases defined in `REDUCED_SCOPE_DATA_MODEL.md`
(`REDUCED_SCOPE_ROADMAP.md`). Permission schemes, workflow versions/drafts, custom fields, saved filters,
sprints, notifications schemes, jobs, event log, and webhooks are not part of the V1 plan at all -- see
`docs/REMOVED_AND_DEFERRED_FEATURES.md`.
