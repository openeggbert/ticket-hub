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
  D20/D79). Adds `ticket_watchers` and `ticket_votes`.
- `007_ranking.sql` — simple integer manual ordering (Phase 3 of `REDUCED_SCOPE_ROADMAP.md`, D31). Drops
  the never-used `tickets.rank_value TEXT` LexoRank placeholder and adds `tickets.rank_order INTEGER NOT
  NULL DEFAULT 0`, backfilled from `ticket_number` for any rows already present at migration time. Since
  `002_seed_demo.sql` runs outside this checksummed flow (see below) and can run before or after this
  migration, it sets `rank_order` explicitly in its own `INSERT` rather than relying on the backfill.
- `008_comment_editing.sql` — comment edited-flag (Phase 4 of `REDUCED_SCOPE_ROADMAP.md`, D81). Adds
  `comments.edited_at`, nullable, set by `IDatabase::editComment` on every edit.
- `009_comment_reactions.sql` — fixed emoji reactions on comments (Phase 4 of
  `REDUCED_SCOPE_ROADMAP.md`, D84). Adds `comment_reactions`, a three-column composite-primary-key
  many-to-many table (`comment_id`, `user_id`, `reaction_key`) mirroring `ticket_watchers`/`ticket_votes`,
  with `reaction_key` constrained to a fixed eight-value set.
- `010_mentions_and_notifications.sql` — @mention handles and the fixed in-app notification set (Phase 4
  of `REDUCED_SCOPE_ROADMAP.md`, D56/D80/D14). Adds `users.handle` (nullable, unique via a partial index
  since SQLite's `ALTER TABLE ADD COLUMN` cannot itself carry a `UNIQUE` constraint) and the `notifications`
  table.
- `011_worklogs.sql` — simplified worklogs (Phase 4 of `REDUCED_SCOPE_ROADMAP.md`, D12/D13). Adds
  `worklogs`, with a tombstone delete (`deleted_at`/`deleted_by_user_id`) and the same optimistic-locking
  `version` column comments/tickets already use.
- `012_audit_log.sql` — simple append-only admin/security audit log (Phase 4 of
  `REDUCED_SCOPE_ROADMAP.md`, D23, the last item in Phase 4). Adds `audit_events`; rows are never
  updated or purged.
- `013_board_columns.sql` — Kanban board WIP limits (Phase 5 of `REDUCED_SCOPE_ROADMAP.md`, D32/D33).
  Adds `board_columns`, a single flat, installation-wide table with no `board_id`/`project_id` column at
  all -- one row per fixed workflow status, not one per project per status.
- `014_attachments.sql` — attachments (Phase 5 of `REDUCED_SCOPE_ROADMAP.md`, D15/D98-D105, the last item
  in Phase 5). Adds only an `ticket_id` index on `attachments` -- `sha256`/`deleted_at`/
  `deleted_by_user_id` already existed from `003_product_foundation.sql`, pre-provisioned ahead of this
  phase.
- `015_personal_access_tokens.sql` — personal access tokens (Phase 6 of `REDUCED_SCOPE_ROADMAP.md`,
  D39/D40). Adds `personal_access_tokens`, mirroring `sessions` plus `name`/`last_used_at`/`revoked_at`.
- `016_ticket_terminology.sql` — post-V1 follow-up, full "issue" -> "ticket" terminology rename
  (product name is Ticket Hub). Renames every `issue*` table (`issues` -> `tickets`, plus
  `issue_history`/`issue_key_aliases`/`issue_labels`/`issue_links`/`issue_statuses`/`issue_types`/
  `issue_votes`/`issue_watchers`) and every `issue_id`/`issue_number`/`issue_key`/`issue_type_id`/
  `parent_issue_id`/`next_issue_number` column across the schema, plus every index name. Pure rename, no
  data or behavior change; SQLite auto-rewrites the affected `CHECK`/`FOREIGN KEY` definitions and
  PostgreSQL's OID-based dependency tracking needs no special handling at all. The PostgreSQL migration
  additionally renames the auto-generated constraint names (primary keys, unique constraints, foreign
  keys, checks) for full consistency, since a table/column `RENAME` does not rename those on its own.
- `017_project_components.sql` — post-V1 follow-up, project components (D19, `KEEP_FOR_V1`), the one
  decided-and-scoped-for-V1 feature that was never actually implemented. Adds `project_components` and
  `tickets.component_id` (nullable, `ON DELETE SET NULL`). See "`project_components`" below for the full
  shape.

`002_seed_demo.sql` remains an explicitly invoked, idempotent development seed rather than a schema migration. It now also inserts a dev-only Argon2id password hash (`demo12345`) into `local_credentials` for all three demo users, an explicit `rank_order` (equal to `ticket_number`) for each seeded ticket, (since `010_mentions_and_notifications.sql`, which `seed-demo` always applies first) a `handle` for each of the three demo users, and (since `013_board_columns.sql`) one `board_columns` row per fixed workflow status, with "In Progress" given a demo WIP limit of 3.

## Current tables

### `schema_migrations`

`version` PK, `checksum`, `applied_at`.

### `users`

`id`, `display_name`, `email` (unique), `handle` (nullable, unique via a partial index -- migration
`010_mentions_and_notifications.sql`, D56), `avatar_url`, `active`, `time_zone`, `clock_format`,
`is_admin`, `created_at`, `updated_at`. `handle` is set only at account creation
(`ticket-hub-cli create-user ... --handle=<handle>`); there is no self-service profile-editing flow yet
to change it afterward. Always stored lowercase (`Domain::normalizeHandle`), same normalization style as
email.

`time_zone` (default `UTC`) and `clock_format` (default `24h`) are self-service (D45): `PATCH
/api/v1/account/preferences` (`IDatabase::updateUserPreferences`) lets a logged-in user set both directly,
validated by `Domain::validateUpdatePreferences` (non-empty IANA-style zone name up to 80 characters,
`clockFormat` exactly `12h` or `24h`). They flow into `Domain::Principal` (via `AuthService::toPrincipal`)
so every response that returns the caller's principal reflects the current values. The web client tracks
"has this browser's user ever touched this" in `localStorage` (not a server column, deliberately -- see
`Models.h`) and only auto-fills from `Intl.DateTimeFormat().resolvedOptions().timeZone` on first login,
never overwriting a value the user set themselves, including a deliberate `UTC`.

The prototype's temporary `username` column was removed in `004_identity.sql`. There is no `handle` column yet — it is added in a later phase together with @mentions, the first feature that actually needs one (`docs/REMOVED_AND_DEFERRED_FEATURES.md`).

### `local_credentials`

`user_id` PK/FK to `users(id)`, `password_hash` (Argon2id-encoded), `failed_login_count`, `locked_until` nullable, `created_at`, `updated_at`.

Minimal login-attempt lockout only (locks for 15 minutes after `IDatabase::MaxFailedLoginAttempts` consecutive failures), per-account. This is now complemented -- not replaced -- by a Phase 6 IP-based fixed rate limiter (D124/D125) on the `/api/v1/auth/login` route itself (20 attempts per IP per 15 minutes), implemented as an in-memory `TicketHub::Web::RateLimiter` in `src/web/RateLimiter.h/.cpp` rather than as database state, since it is process-lifetime-only and keyed by IP rather than by account.

### `sessions`

`id`, `user_id` FK to `users(id)`, `token_hash` (SHA-256 hex, unique), `created_at`, `expires_at`.

The raw session token is never stored, only its SHA-256 hash; it is returned to the caller exactly once, at login. 30-day fixed lifetime. The active-session list (`GET /api/v1/sessions`) and "sign out everywhere" (`POST /api/v1/sessions/sign-out-others`) endpoints (Phase 6, D54) are now implemented -- `IDatabase::listSessionsForUser`/`deleteOtherSessionsForUser`, no new migration needed. "Sign out everywhere" keeps the caller's own current session active and only removes the others (a conservative default; no decision text specifies this).

### `personal_access_tokens`

`id`, `user_id` FK to `users(id)`, `name`, `token_hash` (SHA-256 hex, unique), `created_at`, `expires_at`,
`last_used_at` (nullable), `revoked_at` (nullable) -- Phase 6, D39/D40, migration
`015_personal_access_tokens.sql`. Mirrors `sessions` closely: the raw token is never stored, only
returned once at creation, hashed with the same SHA-256 convention. Unlike sessions, a token also carries
a user-chosen `name` (to tell multiple tokens apart) and can be `revoked_at` before it naturally expires.
`IDatabase::findPersonalAccessTokenByHash` filters out expired/revoked tokens at the SQL layer, exactly
like `findSessionByTokenHash` does for `expires_at`. No scopes -- a token authenticates with exactly its
owner's permissions, resolved the same way a session does (`AuthService::validatePersonalAccessToken` ->
`Domain::Principal`). Self-service only: `TicketService`/`Api.cpp` never expose another user's tokens, and
`revokePersonalAccessToken` is scoped to `(tokenId, userId)` so a non-owner's revoke attempt is a silent
no-op (`false`), not a 403 -- there is nothing to distinguish "not yours" from "doesn't exist" here, and
neither should be revealed.

### `projects`

`id`, `project_key`, `name`, `description`, `lead_user_id`, `next_ticket_number`, `archived`, `created_at`, `updated_at`, `archived_at`, `deleted_at`, `deleted_by_user_id`.

The application allocates ticket numbers transactionally. PostgreSQL locks the project row; SQLite uses `BEGIN IMMEDIATE` and an adapter mutex.

Archiving (D87) and the recycle bin (D88/D89) are now enforced at the application layer, not just schema
columns: `listProjects` excludes both archived and soft-deleted projects from the active list;
`listDeletedProjects` purges anything with `deleted_at` older than 90 days before returning results (no
background job -- purge happens on next access); the project key stays reserved (the `project_key`
column keeps its `UNIQUE` constraint across soft-deleted rows) until `permanentlyDeleteProject` (D90).

### `project_key_aliases`

`alias_key` PK, `project_id` nullable, `created_at`. A null project ID can preserve key reservation after a future permanent deletion.

Populated by `IDatabase::changeProjectKey` (D91, `PATCH /api/v1/projects/<key>/key`, project-Admin-or-above):
the vacated key becomes a permanent row here (so it can never be reused by another project, mirroring
D90's "reserved while in the recycle bin" rule), and every ticket that belonged to the project -- including
soft-deleted ones -- is renamed to the new prefix with the same numeric suffix, its own vacated key becoming
a `ticket_key_aliases` row exactly like `moveTicket` (D38) already does for a single ticket. Ticket lookups
already resolve `ticket_key_aliases` universally, so old ticket URLs/bookmarks keep working after a project
rename; `project_key_aliases` itself is a reservation record, not a lookup index -- there is no route that
resolves a project by its old key (the API has no single-project-by-key GET route at all; every project
reference from the web client goes through `listProjects`, which only returns live keys).

### `project_members`

`project_id`, `user_id`, `role_key`, `joined_at`; composite PK.

`role_key` is one of the three fixed V1 roles (`Domain::ProjectRoleViewer`/`Member`/`Admin`; D3) -- there
are no configurable permission schemes. A user with no membership row (or an unrecognized `role_key`) has
no access to that project's writes; `Domain::projectRoleRank` returns -1 for both cases so callers cannot
tell them apart. A `users.is_admin` global administrator bypasses this check entirely and is implicitly
Admin on every project. `createProject` inserts the creator as project Admin automatically.

### `installation_settings`

`setting_key` PK, `value`, `updated_at`. A small generic key/value store for the handful of
installation-level toggles that survived scope reduction: `anonymous_read_access` (`true`/`false`, absent
means disabled), D59; and, since Phase 7 (D112), `latest_known_version` (a free-text version string, set
by a global administrator, absent means no admin banner is shown -- see `GET`/
`PUT /api/v1/settings/latest-known-version`). Not a general settings framework
(`REDUCED_SCOPE_DATA_MODEL.md` section A).

### Fixed/reference data

- `ticket_types`: key, name, icon, color, hierarchy level.
- `ticket_statuses`: key, name, one of `todo`, `in_progress`, `done`, sort order.
- `priorities`: fixed key/name/rank/color rows.

The demo seed includes Epic, Story, Task, Bug and Sub-task. Hierarchy is enforced at the application
layer (Phase 3 of `REDUCED_SCOPE_ROADMAP.md`), not by a database constraint: `ticket_types.hierarchy_level`
isn't consulted at all -- `Domain::ticketTypeHierarchyLevel` hardcodes the fixed Epic(1)/Story-Task-Bug(0)/
Sub-task(-1) levels (D5, D29, D64-D66), since there are no custom types in V1.

### `tickets`

Core columns:

- identity: `id`, `project_id`, `ticket_number`, `ticket_key`,
- content: `summary`, `description`,
- classification: `ticket_type_id`, `status_id`, `priority_id`, `resolution`,
- people: `reporter_user_id`, `assignee_user_id`,
- hierarchy: `parent_ticket_id`,
- planning: `story_points`, `due_date`, `rank_order`,
- lifecycle: `created_at`, `updated_at`, `version`, `deleted_at`, `deleted_by_user_id`.

Ordinary list, detail and dashboard queries exclude deleted tickets. Status changes increment `version`; an expected stale version raises a concurrency conflict.

`IDatabase::editTicket` (Phase 3, D129) is a full-replacement edit of `summary`/`description`/
`priority_id`/`assignee_user_id`/`story_points`/`due_date`/labels/`ticket_type_id`/`parent_ticket_id`,
sharing the same optimistic-locking contract as `changeTicketStatus`. Re-typing and re-parenting a ticket
after creation (post-V1 follow-up) re-validates the fixed hierarchy shape (Epic/Sub-task/same-project/
parent-level, same rules as `createTicket`) and, transactionally inside `editTicket` itself (not
`TicketService`, since it depends on concurrent database state exactly like `moveTicket`'s own "has
children" rule), rejects retyping across hierarchy levels (Epic <-> Story/Task/Bug <-> Sub-task) whenever
the ticket currently has child tickets -- same-level retyping (e.g. Task -> Bug) is always allowed since it
never changes what any existing parent/child relationship requires.

`deleted_at`/`deleted_by_user_id` are now a real recycle bin, not just schema foundations (Phase 3, D22):
`IDatabase::softDeleteTicket`/`restoreTicket`/`listDeletedTickets`/`permanentlyDeleteTicket` mirror the
project recycle bin exactly (Phase 2, D88/D89) -- fixed 90-day on-demand retention purged inside
`listDeletedTickets`, no background job. `ticket_key`'s own `UNIQUE` constraint keeps the key reserved
while soft-deleted; permanent deletion cannot cause key reuse because a project's `next_ticket_number`
is never decremented. `TicketService::deleteTicket` requires project-Admin-or-above (mirrors
`deleteProject`); restore/list/permanent-delete are global-administrator-only, the same split as D88.

Simple bulk actions (Phase 3, D36) are not a separate database code path: `TicketService::
bulkChangeStatus`/`bulkAssign`/`bulkAddLabel`/`bulkDelete` each loop over a list of ticket keys and call
the corresponding single-ticket operation (`changeStatus`/`editTicket`/`deleteTicket`) independently per
key, so a bulk call carries exactly the same authorization, validation, and workflow rules as doing each
action one at a time. There is no cross-ticket transaction -- a `Domain::BulkActionResult`
(`succeeded`/`failed` ticket-key lists) reports which keys went through rather than rolling back on a
partial failure.

`TicketService::cloneTicket` (D60, Phase 3) copies `summary`/`description`/`ticket_type_id`/`priority_id`/
labels into a new ticket in the same project via the existing `createTicket` path (so the new ticket gets a
fresh key, hierarchy validation, etc. for free), then records a `clones` link back to the original. It
does **not** copy assignee, story points, due date, attachments, sub-tasks, or other links -- and does
not copy the parent/Epic link either, with one structural exception: cloning a Sub-task keeps its
original `parent_ticket_id`, since a Sub-task cannot exist without one (D64) and dropping it would create
an invalid ticket, not merely an incomplete copy.

`parent_ticket_id` now has application-layer meaning (Phase 3): `TicketService::createTicket` rejects a
request that violates the fixed hierarchy (a Sub-task without a parent, a parent of the wrong type, or a
parent in a different project) with `std::invalid_argument` before the row is ever inserted.

`rank_order` (Phase 3, D31) replaces the never-used `rank_value TEXT` LexoRank placeholder: a plain
per-project integer, renumbered in a full pass rather than shifted minimally. `IDatabase::reorderTicket`
fetches the project's live ticket-id list ordered by `(rank_order, ticket_number)`, removes the moving
ticket, re-inserts it immediately before a given anchor ticket (or appends it if no anchor is given), then
writes back sequential ranks `1..N` for the whole list in the same transaction. A cross-project anchor or
the ticket itself as the anchor is rejected with `std::invalid_argument`. `TicketService::reorderTicket`
requires project-Member-or-above on the ticket's own project (reordering is always single-project).

`IDatabase::moveTicket` (Phase 3, D37) moves a ticket to a different project. No compatibility check is
needed -- every project shares the same fixed types/workflow/fields (D4/D9) -- so a move is exactly a
`project_id` change plus a freshly allocated key/number in the target project, using the same
counter/locking mechanism as `createTicket`, plus an append-at-end `rank_order` in the target project. It
is rejected with `std::invalid_argument` if the ticket has a parent, has any (non-deleted) children, is
already in the target project, or the target project is unknown/archived/deleted -- hierarchy (D64-D66)
requires a parent and its children to share a project, and `moveTicket` itself does not re-parent/un-parent
as part of the move (a ticket with a parent or children must be edited via `editTicket` first to clear
them). The vacated key is written into `ticket_key_aliases` as a permanent alias (D38) -- the first
code path that actually writes to that table -- and the move records one `ticket_history` row
(`field_name = 'project'`). `TicketService::moveTicket` requires project-Member-or-above on **both** the
source and target projects, mirroring `createTicketLink`'s two-project-role-check pattern.

`resolution` (`fixed`/`done`/`wont-fix`/`duplicate`/`cannot-reproduce`, CHECK-constrained since
`001_initial.sql`) is now set and cleared by `changeTicketStatus` itself, transactionally with the status
update (D68-D70): required when the target status's `category` is `done` (missing or unrecognized ->
`Domain::WorkflowViolation`), forced to `NULL` when leaving a `done`-category status ("reopening"), and
left untouched for any other transition. The same transaction also rejects completing a ticket
(`Domain::WorkflowViolation`) while any non-deleted child (`parent_ticket_id` pointing at it) has a
non-`done`-category status -- the fixed "sub-task completion gate" (D68). Reopening a parent never
touches its children's rows (D69) -- there is no cascade to implement.

### `ticket_key_aliases`

`alias_key` PK, `ticket_id` nullable, `created_at`. Detail and comment operations resolve both current and
alias keys. Until Phase 3's `moveTicket` (D37/D38), this table was schema-only (no code path ever wrote to
it); `moveTicket` now inserts the vacated key here on every move, and it is safe from collision because
`alias_key` is itself a `PRIMARY KEY` and ticket numbers/keys are never reused.

### Labels

- `labels`: `id`, globally normalized `name`, color.
- `ticket_labels`: ticket/label composite PK.

Application services trim, lowercase and deduplicate new labels.

### `project_components`

`id`, `project_id`, `name`, `description`, `lead_user_id` nullable, `default_assignee_user_id` nullable,
`created_at`, `updated_at`; `UNIQUE(project_id, name)` -- migration `017_project_components.sql` (D19,
`KEEP_FOR_V1`). "Simple project components: name, description, lead, default assignee; at most one per
ticket" -- already the cheapest reasonable form per the decision text ("small table plus one optional
ticket field"), kept exactly as originally decided. `tickets.component_id` (nullable, `ON DELETE SET
NULL`) is that one optional field. No recycle bin/soft-delete columns, unlike tickets/projects/comments --
D19 does not call for one; `IDatabase::deleteComponent` is a plain hard delete, and any ticket that
referenced the deleted component simply has `component_id` cleared via the foreign key's `ON DELETE SET
NULL`, not rejected or cascaded. `IDatabase::createComponent`/`editComponent` resolve `leadEmail`/
`defaultAssigneeEmail` the same way `createTicket`/`editTicket` resolve `assigneeEmail` -- a human-readable
identifier in the request, not a raw user id -- and a duplicate `(project_id, name)` throws
`std::invalid_argument` (the table's own `UNIQUE` constraint). `TicketService::createComponent`/
`editComponent`/`deleteComponent` require project-Admin-or-above, the same level as archiving/deleting a
project itself; `listComponents` shares the read-access rule every other project/ticket read uses (any
authenticated user, or anonymous if the installation toggle is on). `editComponent`/`deleteComponent` are
scoped to `(projectKey, componentId)` together, not `componentId` alone -- the same IDOR-safe pattern
`editWorklog`/`deleteAttachment` already established -- so a component id belonging to a different project
is treated as not found, never silently acted on through the wrong project's URL.
`TicketService::createTicket`/`editTicket` resolve a `componentName` in the request against the ticket's
own project's components (`std::invalid_argument` if unknown); `TicketService::cloneTicket` (D60) copies
the component into the clone, per the original decision text's own "summary/description/type/priority/
labels/component" list for cloning. `Domain::TicketFilter::componentName` filters ticket lists the same
way `label` does (case-insensitive match against the ticket's own component name, no cross-project
ambiguity since a ticket's `component_id` already points at exactly one project's component).

### Comments

`id`, `ticket_id`, `author_user_id`, `body`, `created_at`, `updated_at`, `version`, `deleted_at`,
`deleted_by_user_id`, `edited_at` (migration `008_comment_editing.sql`, Phase 4, D81).

Ordinary comment lists exclude deleted comments. `IDatabase::editComment` (D81) is a full-replacement edit
of `body` sharing the same optimistic-locking contract as `editTicket`/`changeTicketStatus`
(`expectedVersion` -> `Domain::ConcurrencyConflict`) and sets `edited_at` -- there is no stored history of
prior text, only the fact that an edit happened. `IDatabase::deleteComment` (D82) is a tombstone delete:
sets `deleted_at`/`deleted_by_user_id`, same mechanism as tickets/projects; the row and original body stay
in the database (visible to a direct query, not through any V1 API) since there is no separate admin
recycle-bin API for comments, unlike tickets and projects -- the existing soft-delete columns are the whole
mechanism this decision calls for. `TicketService::editComment`/`deleteComment` (D83) use simplified
permissions: the comment's own author may always edit/delete it; otherwise the actor needs
project-Admin-or-above (or global admin) on the comment's ticket's project -- no separate
edit-own/edit-all/delete-own/delete-all permission matrix.

There is no `comments.body` full-text scan beyond @mention parsing (D80): `TicketService::addComment`
extracts every distinct `@handle` token from the body with a plain regex, resolves each against
`users.handle` (`IDatabase::findUserByHandle`), and creates a `mentioned` notification for each resolved
user other than the comment's own author. This only happens on creation, not on every `editComment` save,
to avoid re-notifying on every edit of an already-mentioning comment. An unresolvable handle (typo, or a
user with no handle set) is silently ignored, not an error.

### `notifications`

`id`, `user_id`, `type` (`CHECK` constrained to `assigned`/`mentioned`/`watched_comment`), `ticket_id`
(nullable, `ON DELETE CASCADE`), `read_at` (nullable), `created_at` -- migration
`010_mentions_and_notifications.sql` (Phase 4, D14). This is the fixed in-app notification set: no
delivery-channel column (in-app only, no email), no admin-configurable schemes, no per-user
preferences/digests -- matches the minimal shape in `docs/REDUCED_SCOPE_DATA_MODEL.md` exactly. There is
no stored message string; `IDatabase::listNotifications`/`createNotification` resolve `ticketKey`/
`ticketSummary` at read time via a `LEFT JOIN` on `tickets`, and the API/UI build display text from `type` +
the resolved ticket.

All three types are created as a side effect of an existing write, never directly by an API caller:

- `assigned`: `TicketService::createTicket`/`editTicket` compare the ticket's assignee before and after the
  write; a newly-set or changed assignee is notified, but assigning to yourself, or an edit that leaves
  the assignee unchanged, notifies nobody.
- `mentioned`: see the Comments section above.
- `watched_comment`: `TicketService::addComment` notifies every current watcher of the ticket
  (`IDatabase::listWatchers`) except the comment's own author.

A recipient who would receive both `mentioned` and `watched_comment` from the same comment (mentioned
*and* already watching) gets only `mentioned` -- one notification per comment per recipient, the more
specific reason wins; this is a deliberate simplification, not a stored dedupe key.
`markNotificationRead`/`markAllNotificationsRead` are always scoped to the caller's own `user_id`; there
is no cross-user notification management.

### `ticket_history`

`id`, `ticket_id`, `actor_user_id`, `field_name`, `old_value`, `new_value`, `created_at`.

Records status changes (`field_name = 'status'`) and, since Phase 3's `editTicket`, one row per
standard field that actually changed value on a full-field edit (`field_name` one of `summary`,
`description`, `priority`, `assignee`, `story_points`, `due_date`) -- a field left unchanged writes no
row. The target replaces these display-oriented strings with fully typed structured history.

### `ticket_links`

`id`, `source_ticket_id`, `target_ticket_id`, `link_type`, `created_at`; `CHECK (source_ticket_id <>
target_ticket_id)`.

The fixed link-type catalog (D17, Phase 3) is enforced at the application layer, not by a database
CHECK/enum: `Domain::isValidLinkType` accepts exactly `blocks`, `relates_to`, `duplicates`, `clones` --
there is no admin-configurable catalog. A link is one directed row but is meaningful from either end;
`IDatabase::listTicketLinks(ticketKey)` returns it from both the source's ("outward", label e.g.
`blocks`) and the target's ("inward", label e.g. `is blocked by`) perspective via
`Domain::linkTypeLabels`. `relates_to` uses the same label both ways. `IDatabase::createTicketLink`
rejects an exact-duplicate `(source, target, linkType)` triple; `TicketService::createTicketLink`
additionally requires project-Member-or-above on **both** tickets' projects, since a link write touches
two tickets that may be in different projects. `TicketService::cloneTicket` (D60) creates one automatically
(`clones`, clone -> original) whenever a ticket is cloned.

### `ticket_watchers` and `ticket_votes`

Both: `ticket_id`, `user_id`, `created_at`; composite PK `(ticket_id, user_id)`; `ON DELETE CASCADE` on
both foreign keys. Structurally identical -- no other columns, since D20/D79 keep both features to
exactly "who is watching/voting", with no priority-change side effect from votes and no
authorized-user-manages-others-watchers flow. `TicketService::watchTicket`/`voteTicket` and their
`unwatch`/`unvote` counterparts are the entire write surface, and deliberately have **no project-role
check** -- unlike every other ticket write, only project-Member-or-above -- since Jira itself gates
watch/vote by "browse" access rather than a write-capable role, and this reduced model's closest
equivalent is simply being an authenticated user (D58). `watchTicket`/`voteTicket` return `true` only when
the row was newly inserted (idempotent on a repeat call); `unwatchTicket`/`unvoteTicket` return `true` only
when a row was actually removed. `IDatabase::listWatchedTickets(userId, limit)` (Phase 5, D24) is the
reverse direction of `listWatchers` -- every non-deleted ticket a given user is watching, newest-updated
first -- backing the personal dashboard's "watched tickets" widget; no equivalent reverse query exists for
votes, since D24's dashboard has no "voted tickets" widget.

### `comment_reactions`

`comment_id`, `user_id`, `reaction_key`, `created_at`; composite PK `(comment_id, user_id, reaction_key)`;
`ON DELETE CASCADE` on both foreign keys; `reaction_key` constrained (`CHECK`) to a fixed set:
`thumbs_up`, `thumbs_down`, `laugh`, `hooray`, `confused`, `heart`, `rocket`, `eyes` -- GitHub's
well-known eight-reaction set, chosen as a conservative default since the decision register (D84) calls
for "a fixed reaction set" without enumerating one. Structurally the same shape as
`ticket_watchers`/`ticket_votes` with one extra key column for the reaction itself, since a user may add
more than one distinct reaction to the same comment (just not the same reaction twice).
`TicketService::addCommentReaction`/`removeCommentReaction` mirror `watchTicket`/`voteTicket` exactly: no
project-role check, just an authenticated actor and an existing comment (an unknown ticket, comment, or
reaction key throws `std::invalid_argument`, matching `watchTicket`'s own unknown-ticket behavior rather
than `editComment`/`deleteComment`'s nullopt/false convention). `IDatabase::addCommentReaction`/
`removeCommentReaction` return `true` only when a row was actually inserted/removed (idempotent on a
repeat call). `IDatabase::listCommentReactions` returns the raw `(reactionKey, user)` rows for a comment;
the API and UI group them by `reactionKey` for per-reaction counts and highlighting, the same
server-stays-dumb/client-aggregates split used for ticket links.

### `worklogs`

`id`, `ticket_id`, `author_user_id`, `work_date` (`DATE` in PostgreSQL, `TEXT` ISO date in SQLite, same
convention as `tickets.due_date`), `time_spent_seconds`, `comment` (nullable), `deleted_at`,
`deleted_by_user_id`, `created_at`, `updated_at`, `version` -- migration `011_worklogs.sql` (Phase 4,
D12/D13). No `remaining_adjustment_mode`/`remaining_estimate_seconds_after` columns from the original
baseline schema (`docs/DATA_MODEL.md`): D12 dropped time estimates from V1 entirely, so there is nothing
for a worklog to adjust. `IDatabase::editWorklog` shares `editComment`/`editTicket`'s optimistic-locking
contract (`expectedVersion` -> `Domain::ConcurrencyConflict`); `IDatabase::deleteWorklog` is a tombstone
delete, same mechanism as comments/tickets/projects. `TicketService::addWorklog`/`editWorklog`/
`deleteWorklog` all require only project-Member-or-above on the ticket's project (D13: no separate
own-vs-others permission split) -- unlike comments (D83's author-or-admin rule), any project member may
edit or delete *any* worklog on a ticket they can access, not just the one they logged themselves.

### `audit_events`

`id`, `category`, `action`, `actor_user_id` (nullable, `ON DELETE SET NULL`), `target_type` (nullable),
`target_id` (nullable), `details` (nullable), `created_at` -- migration `012_audit_log.sql` (Phase 4,
D23, the last item in Phase 4). Simplified from the original baseline schema (`docs/DATA_MODEL.md`):
no `actor_type`/`project_id`/`ip_address`/`correlation_id`/`before_json`/`after_json`/`metadata_json`
columns, and no separate `audit_retention_policies` table -- rows are simply appended and never updated
or purged, matching D23's "no categories[-as-a-retention-feature], export, or configurable retention"
simplification. `actor_user_id` is nullable because the only account-creation path
(`ticket-hub-cli create-user`) runs outside any web session and has no `Principal` to attribute the
event to, and a failed/blocked login attempt has no authenticated actor by definition.
`IDatabase::recordAuditEvent` is fire-and-forget (returns `void`, unlike `createNotification`, whose
result the notification list feature reads back immediately); `listAuditEvents(limit)` is newest-first,
capped, with no pagination/filtering. Recorded automatically as a side effect of a small, focused set of
existing writes -- `AuthService::login` (failed/blocked) and `createUser`, `TicketService::
setAnonymousReadEnabled`, `permanentlyDeleteProject`, `permanentlyDeleteTicket` -- not a general-purpose
audit hook on every write. `TicketService::listAuditEvents` is global-administrator-only, like the
recycle bins.

### `board_columns`

`id`, `status_id` (unique FK to `ticket_statuses`), `wip_limit` (nullable), `sort_order` -- migration
`013_board_columns.sql` (Phase 5, D32/D33). A single flat, installation-wide table, not one row per
project per status: D32 keeps "one board column equals one workflow status" and the reduced-scope data
model's target schema for this table lists only `status_id`/`wip_limit`/`sort_order`, with no
`board_id`/`project_id` column at all. A WIP limit set on a column therefore applies to that status's
column on every project's board -- there is no per-project board identity in the reduced-scope model.
`wip_limit` is always soft: `IDatabase::setBoardColumnWipLimit` never blocks a status transition or ticket
creation, it only changes what `web/`'s Board view highlights at display time.
`TicketService::setBoardColumnWipLimit` is global-administrator-only, like the anonymous-read toggle,
since there is no per-project board-admin concept to delegate the setting to instead.

### `attachments`

`id`, `ticket_id`, `uploader_user_id`, `file_name`, `content_type`, `byte_size`, `storage_key`,
`created_at`, `sha256`, `deleted_at`, `deleted_by_user_id` -- fully implemented (Phase 5, D15/D98-D105,
migration `014_attachments.sql`). Attached to a ticket directly, not to an individual comment, so the
same attachment can be referenced via `attachment://<id>` from the ticket description or from any comment
on that ticket (D100). `storage_key` always equals `id`: `IDatabase::createAttachment` is the one create*
method that takes a caller-supplied id (rather than generating one internally) because
`Infrastructure::Storage::LocalAttachmentStorage` needs the key -- and the file needs to already be
written -- before the row is inserted, so a database row never describes a file that doesn't exist on
disk (D15: local filesystem storage only, hardwired, no abstract storage port and no S3 extension
point). `deleted_at`/`deleted_by_user_id` mirror the ticket/project/comment tombstone pattern exactly
(D101); the fixed 90-day on-demand retention (D102) is implemented in `TicketService::
listDeletedAttachments`, not inside the database adapter like `listDeletedTickets`/`listDeletedProjects`
-- purging an attachment also means deleting its file on disk, which the SQL-only `IDatabase` layer
cannot do. `sha256` is computed once, at upload (D105); there is no periodic re-verification.
`Domain::validateAttachmentUpload` enforces D98's fixed limits (25MB/file, 20/ticket, a blocked-extension
denylist) -- no admin configuration, no MIME allow-list, no quotas. `TicketService::permanentlyDeleteTicket`/
`permanentlyDeleteProject` both collect every affected attachment's storage key and delete its file
*before* the database's `ON DELETE CASCADE` removes the rows, since there is no periodic orphan-file
audit at all to catch files left behind afterward.

## Current indexes

Indexes cover project/status/assignee/update ticket access, live ticket listing, comment timelines, aliases, label joins, and session lookup/expiry. Full-text indexes are not planned at all for V1 -- search uses a plain `LIKE`/`ILIKE` query (`docs/REDUCED_SCOPE_SPECIFICATION.md` section 10).

`Domain::TicketFilter` (D10/D43, Phase 5) is the ad-hoc, in-UI-only filter model used by `IDatabase::
listTickets` and `GET /api/v1/tickets` -- no saved/shared filters, no JQL, not usable as a webhook/board
source. It combines `projectKey`, `statusKey`, `ticketTypeKey`, `priorityKey`, `assigneeEmail`, `label`,
`dueBefore` (inclusive `<=`), and `search` (matches summary, description, and ticket key). `label` is
implemented as an `EXISTS` subquery against `ticket_labels`/`labels` rather than a condition on the
already-joined/aggregated label-list column used to *display* a ticket's labels, so filtering by one
label does not truncate a matching ticket's own label list to just that label.

`IDatabase::listTickets(filter)` (unpaginated, used internally and by CSV export) has always had a
hardcoded `LIMIT 200` in its SQL -- previously with no `total` count exposed anywhere, silently
truncating any result set past 200 rows. Phase 6 (D126) added
`IDatabase::listTickets(filter, limit, offset)` and `IDatabase::countTickets(filter)` alongside it (not
replacing it), and `GET /api/v1/tickets` now uses the paginated pair via
`TicketService::listTicketsPaged`, exposing `page`/`pageSize`/`totalItems`/`totalPages` so a caller can
detect and page past what used to be a silent cutoff. `Domain::DefaultPageSize`/`Domain::MaxPageSize`
(both 200, matching the historical cap) mean a caller sending no pagination parameters gets exactly the
same result set as before. This is a deliberate partial rollout -- no other `IDatabase` list method has
a paginated counterpart yet.

## Deliberate implementation gap

The current schema is a migration-safe foundation for the **reduced-scope V1**, not the full original
target model. Phases 1-5 of `docs/REDUCED_SCOPE_ROADMAP.md` are all complete at the core/CLI/test/server/UI
layer (identity and sessions; authorization and project lifecycle; the fixed workflow/hierarchy, full
ticket edit, links, cloning, watching/voting, the ticket recycle bin, bulk actions, and manual
ordering/moving; comment editing, reactions, mentions/notifications, Markdown rendering, worklogs, and the
admin/security audit log; ad-hoc ticket filtering, the personal dashboard, Kanban board WIP limits, and
attachments). Re-typing (`ticketTypeKey`) and re-parenting (`parentTicketKey`) a ticket after creation --
long the one remaining gap in the roadmap's own scope -- was added as the first piece of optional,
non-roadmap follow-up after the reduced-scope V1 roadmap itself closed (see `NEXT.md`). Permission schemes,
workflow versions/drafts, custom fields, saved filters, sprints, notifications schemes, jobs, event log,
and webhooks are not part of the V1 plan at all -- see `docs/REMOVED_AND_DEFERRED_FEATURES.md`.
