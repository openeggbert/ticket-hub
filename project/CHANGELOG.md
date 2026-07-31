# Changelog

## Unreleased — Phase 3 (complete at the core/CLI/test layer): manual ordering and moving issues between projects (reduced scope)

- Added migration `007_ranking.sql` (both backends): drops the never-used `rank_value TEXT` LexoRank
  placeholder (added in `003_product_foundation.sql`, safe to drop directly since it carried no
  UNIQUE/CHECK/index/FK) and adds `rank_order INTEGER NOT NULL DEFAULT 0`, backfilled from
  `issue_number`. This backfill only reaches rows that already exist at migration-apply time; the demo
  seed (`002_seed_demo.sql`, applied separately from the checksummed migration flow since its filename
  matches `discoverMigrationFiles`'s `_seed_` exclusion) now sets `rank_order` explicitly in its own
  `INSERT` so seeded issues get a correct rank regardless of run order.
- Added `IDatabase::reorderIssue` in both adapters (D31): a simple integer rank with a full renumbering
  pass on every move (not a minimal-diff/fractional scheme) -- justified directly by D31's own "sufficient
  for small per-project issue counts" rationale. Moves an issue to immediately before another issue in
  the same project, or to the end of the project when no anchor is given; rejects an anchor in a
  different project or the issue itself as the anchor with `std::invalid_argument`.
- Added `IDatabase::moveIssue` in both adapters (D37): moves an issue to a different project. D37 needs
  no compatibility check (every project shares the same fixed types/workflow/fields), so a move is just
  a `project_id` change plus a freshly allocated key/number in the target project, exactly like creating
  a new issue there. Rejected with `std::invalid_argument` if the issue has a parent, has any children,
  is already in the target project, or the target project is unknown. The vacated key becomes a
  permanent alias (D38) -- the first code path that actually writes to `issue_key_aliases`, which
  previously existed only as an unused schema foundation -- and the move writes one `issue_history` row
  (`field_name = 'project'`).
- Added matching `TicketService::reorderIssue` (project-Member-or-above on the issue's own project) and
  `TicketService::moveIssue` (project-Member-or-above on **both** the source and target projects,
  mirroring `createIssueLink`'s two-project-role-check pattern).
- Added `POST /api/issues/{key}/reorder` and `POST /api/issues/{key}/move` to `Api.cpp`, and `rankOrder`
  to the issue JSON representation (**not yet compiled** -- see "Known verification limitation" in
  `README.md`).
- Extended `sqlite_integration_tests` (renumbering correctness on reorder-before-anchor and
  reorder-to-end, cross-project and self-anchor rejection, move mechanics including the target project's
  rank/counter, alias creation and resolution, `issue_history` write, and rejection of same-project moves,
  unknown-project moves, and moves of an issue with a parent or with children) and
  `authorization_integration_tests` (reorder/move authorization, including the "member of source but not
  target project" case for move). Manually verified `reorderIssue` and `moveIssue` (through
  `PostgresDatabase` directly) against a live local PostgreSQL 16 server.
- This closes Phase 3's remaining core-layer scope; re-typing and re-parenting an issue after creation
  remain deliberately out of scope (see `NEXT.md`).

## Unreleased — Phase 3 (partial, continued): issue recycle bin and bulk actions (reduced scope)

- Added `IDatabase::softDeleteIssue`/`restoreIssue`/`listDeletedIssues`/`permanentlyDeleteIssue` in both
  adapters (D22), mirroring the project recycle bin (Phase 2, D88/D89) exactly: fixed 90-day on-demand
  retention purged inside `listDeletedIssues`, no background job, key stays reserved via `issue_key`'s
  own `UNIQUE` constraint while soft-deleted. Matching `TicketService` methods: `deleteIssue` requires
  project-Admin-or-above (mirrors `deleteProject`); `restoreIssue`/`listDeletedIssues`/
  `permanentlyDeleteIssue` are global-administrator-only, the same split used for projects.
- Added simple bulk actions (D36): `Domain::BulkActionResult` and `TicketService::bulkChangeStatus`/
  `bulkAssign`/`bulkAddLabel`/`bulkDelete`. Each loops over a list of issue keys and calls the
  corresponding single-issue operation independently per key -- identical authorization/validation/
  workflow-rule behavior to doing each action one at a time, no cross-issue transaction. A partial
  failure (unknown key, insufficient role, workflow violation) is reported via the result's
  `succeeded`/`failed` key lists rather than rolling back keys that already went through. No
  cross-project move and no type change in bulk, per D36.
- Added `DELETE /api/issues/{key}`, `GET /api/issues/deleted`, `POST /api/issues/{key}/restore`,
  `DELETE /api/issues/{key}/permanent`, and `POST /api/issues/bulk/{status,assign,label,delete}` to
  `Api.cpp` (**not yet compiled** — see "Known verification limitation" in `README.md`).
- Extended `sqlite_integration_tests` (soft-delete/restore/list-bin/permanent-delete lifecycle,
  idempotent no-ops, comments cascade-deleting with a permanently-deleted issue) and
  `authorization_integration_tests` (project-admin-vs-global-admin split for the recycle bin; bulk
  actions applying the same per-issue authorization, including a mixed batch of accessible/inaccessible/
  unknown issue keys reporting partial success).
- Manually verified the issue recycle bin and all four bulk actions (through `TicketService`) against a
  live local PostgreSQL 16 server.

## Unreleased — Phase 3 (partial, continued): watchers and voting (reduced scope)

- Added migration `006_collaboration.sql` (both backends): `issue_watchers` and `issue_votes`, identical
  `(issue_id, user_id)` composite-PK many-to-many tables with `ON DELETE CASCADE`.
- Added `IDatabase::watchIssue`/`unwatchIssue`/`listWatchers` and `voteIssue`/`unvoteIssue`/`listVoters`
  in both adapters (D20, D79), and matching `TicketService` methods. Both features are self-service only
  and deliberately have **no project-role check** -- the one exception among issue writes -- since
  Jira gates watch/vote by "browse" access rather than a write-capable role; any authenticated user may
  watch/vote on any issue. `watch`/`voteIssue` return `true` only when newly added (idempotent on
  repeat); `unwatch`/`unvoteIssue` return `true` only when a row was actually removed.
- Added `POST`/`DELETE /api/issues/{key}/watch`, `GET /api/issues/{key}/watchers`,
  `POST`/`DELETE /api/issues/{key}/vote`, and `GET /api/issues/{key}/voters` to `Api.cpp` (**not yet
  compiled** — see "Known verification limitation" in `README.md`).
- Extended `sqlite_integration_tests` (watch/vote idempotency, listing, unknown-issue rejection) and
  `authorization_integration_tests` (a non-member of the issue's project can still watch/vote, unlike
  every other write tested).
- Manually verified watch/vote (idempotency, listing, unwatch/unvote, unknown-issue rejection) against a
  live local PostgreSQL 16 server.

## Unreleased — Phase 3 (partial, continued): issue links and cloning (reduced scope)

- Added the fixed issue-link catalog (D17): `Domain::isValidLinkType`/`linkTypeLabels` (`blocks`,
  `relates_to`, `duplicates`, `clones`, each with an outward/inward label pair; `relates_to` uses the
  same label both ways), `Domain::IssueLink` (one link as seen from a given issue) and
  `Domain::IssueLinkDetail` (both ends resolved to their project, for authorization).
- Added `IDatabase::createIssueLink`/`listIssueLinks`/`findIssueLinkById`/`deleteIssueLink` in both
  adapters, and the matching `TicketService` methods. Creating or deleting a link requires
  project-Member-or-above on **both** linked issues' projects (a link write touches two issues that may
  be in different projects, unlike every other issue write). `createIssueLink` rejects an unknown link
  type, a self-link (the database `CHECK` constraint is the backstop), and an exact-duplicate
  `(source, target, linkType)` triple.
- Added simple field-copy cloning (D60): `TicketService::cloneIssue` copies summary/description/type/
  priority/labels into a new issue via the existing `createIssue` path (getting hierarchy validation and
  a fresh key for free), then creates a `clones` link back to the original. Does not copy assignee,
  story points, due date, attachments, sub-tasks, or other links. One structural exception: cloning a
  Sub-task keeps its original parent, since a Sub-task cannot exist without one (D64).
- Added `POST /api/issues/{key}/clone`, `GET`/`POST /api/issues/{key}/links`, and
  `DELETE /api/issue-links/{id}` to `Api.cpp` (**not yet compiled** — see "Known verification limitation"
  in `README.md`).
- Extended `sqlite_integration_tests` (link create/list-from-both-ends/duplicate-rejection/
  self-link-rejection/find-by-id/delete), `workflow_integration_tests` (clone field-copy correctness, the
  sub-task-parent-retention special case, and basic link lifecycle through `TicketService`), and
  `authorization_integration_tests` (role gating for cloning and for links spanning two projects).
- Manually verified issue links (create/list/duplicate-and-self-link rejection/find/delete) and cloning
  (through `TicketService`, including the sub-task special case) against a live local PostgreSQL 16
  server.

## Unreleased — Phase 3 (partial, continued): full issue edit (reduced scope)

- Added `Domain::EditIssueRequest` and `IDatabase::editIssue`/`TicketService::editIssue` (D129): a
  full-replacement edit of an issue's standard fields (summary, description, priority, assignee, story
  points, due date, labels) sharing `changeIssueStatus`'s optimistic-locking contract (`expectedVersion`
  -> `Domain::ConcurrencyConflict` on a mismatch) and requiring the same project-Member-or-above role.
  Returns `nullopt` for an unknown issue rather than throwing. Does not edit `issueTypeKey` or
  `parentIssueKey` -- re-typing or re-parenting an issue is not yet implemented.
- Each changed field writes one `issue_history` row (`summary`/`description`/`priority`/`assignee`/
  `story_points`/`due_date`); an unchanged field writes none.
- Factored the summary/description/priority/storyPoints/labels validation shared between
  `validateCreateIssue` and the new `validateEditIssue` into one internal helper
  (`appendIssueContentErrors`) instead of duplicating it; factored the label
  normalize-sort-dedupe logic shared between `createIssue` and `editIssue` into `normalizeLabels` in
  `TicketService.cpp`.
- Added `PATCH /api/issues/{key}` to `Api.cpp` for the new edit use case (**not yet compiled** — see
  "Known verification limitation" in `README.md`). While there, fixed a real bug: `POST /api/issues`,
  `PATCH /api/issues/{key}/status`, and `POST /api/issues/{key}/comments` were each missing a
  `catch (const Domain::Forbidden&)` handler, so a project-role authorization failure on any of those
  three routes would have fallen through to the generic handler and returned HTTP 500 instead of 403.
- Extended `sqlite_integration_tests` with `editIssue` coverage (every field, label replacement,
  assignee clearing, the stale-version conflict, and one `issue_history` row per changed field) and
  `authorization_integration_tests` with role-gating coverage for `editIssue` (non-member rejected,
  member permitted, unknown issue returns `nullopt`).
- Manually verified `editIssue` (every field, label replacement, assignee clearing, the stale-version
  conflict, and editing an unknown issue) against a live local PostgreSQL 16 server.

## Unreleased — Phase 3 (partial): fixed workflow and hierarchy (reduced scope)

- Added the fixed Epic -> Story/Task/Bug -> Sub-task hierarchy (D5, D29, D64-D66) as an
  application-layer rule (`Domain::issueTypeHierarchyLevel`, `TicketService::requireValidHierarchy`):
  a Sub-task requires a Story/Task/Bug parent, an Epic may not have a parent, a Story/Task/Bug's
  optional parent must be an Epic, and a parent must be in the same project. `CreateIssueRequest`
  gained `parentIssueKey`; `createIssue` now persists `issues.parent_issue_id` (previously write-only
  in name -- the column existed and was read back, but nothing ever set it).
- Added the fixed workflow's hardcoded transition rules (D68-D70), enforced transactionally inside
  `IDatabase::changeIssueStatus` in both adapters (not the application layer, since they depend on
  current database state and must not race with a concurrent change): a transition to a
  Done-category status requires a valid `resolution` (`Domain::isValidResolution`) and is rejected
  with the new `Domain::WorkflowViolation` if any non-deleted child issue is not yet Done-category
  ("cannot complete while sub-tasks are unfinished"); a transition away from Done-category
  ("reopening") always clears `resolution` and never touches child issues; any other transition
  leaves `resolution` untouched. `changeIssueStatus` gained a `resolution` parameter (both
  `IDatabase` and `TicketService::changeStatus`). `Domain::Issue` gained a `resolution` field.
- Added `Domain::WorkflowViolation` (maps to HTTP 422 in `Api.cpp`), distinct from `Domain::Forbidden`
  (authorization) and `Domain::ConcurrencyConflict` (stale version): the caller is authorized and the
  request is well-formed, but the current state doesn't allow it.
- Updated `Api.cpp`: `POST /api/issues` accepts `parentIssueKey`; `PATCH /api/issues/{key}/status`
  accepts `resolution` and maps `Domain::WorkflowViolation` to 422; issue JSON responses include
  `resolution` (**not yet compiled** — see "Known verification limitation" in `README.md`).
- Added `workflow_integration_tests` (SQLite, through `TicketService`): every hierarchy-rejection case,
  resolution required/rejected-if-unknown on the transition to Done, resolution cleared on reopen, the
  sub-task-completion gate blocking and then permitting a parent's completion, and reopening a parent
  leaving its sub-task's status untouched.
- Manually verified `createIssue` (with `parentIssueKey`) and `changeIssueStatus` (with `resolution`,
  the sub-task gate, and the reopen-clears-resolution rule) against a live local PostgreSQL 16 server.
- **Not yet done from Phase 3** (see `NEXT.md`): full issue edit beyond status (summary/description/
  priority/assignee/labels/due date changes with optimistic locking), simple cloning (D60), the fixed
  issue-link catalog (D17), self-only watchers (D20), voting (D79), simple bulk actions (D36),
  always-allowed project moves (D37), and the integer rank/renumber migration (D31) replacing the
  unused `issues.rank_value` text column. No web UI changes for any of Phase 3 (no hierarchy picker,
  no resolution field on the status-change form) -- that depends on the still-unverified server target.

## Unreleased — Phase 2: authorization and projects (reduced scope)

- Added fixed project roles (`Domain::ProjectRoleViewer`/`Member`/`Admin`, `Domain::projectRoleRank`) and
  a global-administrator bypass, enforced in `TicketService` via new `requireProjectRole`/
  `requireGlobalAdmin` helpers that throw `Domain::Forbidden` (HTTP 403). `createIssue`, `changeStatus`,
  and `addComment` now require Member-or-above on the issue's project.
- Added project lifecycle to `IDatabase`/`SqliteDatabase`/`PostgresDatabase` and wrapped it in
  `TicketService`: `createProject` (global admin), `setProjectArchived` (project admin), `deleteProject`
  (project admin, soft delete to the recycle bin), `restoreProject`/`listDeletedProjects`/
  `permanentlyDeleteProject` (global admin only, per D88's "admin restore or permanent delete"). The
  recycle bin purges anything past the fixed 90-day retention on access; there is no background job
  (D89, D51).
- Added migration `005_authorization.sql` (both backends): `installation_settings` generic key/value
  table for the small number of remaining installation-level toggles.
- Added the installation-wide anonymous read-access toggle (D59, off by default,
  `TicketService::isAnonymousReadEnabled`/`setAnonymousReadEnabled`). Every read use case
  (`listProjects`, `listIssues`, `findIssue`, `listComments`, `dashboard`) now takes an
  `std::optional<Principal>`; an anonymous caller is rejected with the new `Domain::AuthenticationRequired`
  (HTTP 401) unless the toggle is on. Any authenticated user still sees all projects regardless of
  membership — roles gate writes only (D58).
- Added `POST /api/projects`, `PATCH /api/projects/{key}/archived`, `DELETE /api/projects/{key}`,
  `GET /api/projects/deleted`, `POST /api/projects/{key}/restore`, `DELETE /api/projects/{key}/permanent`,
  and `GET`/`PUT /api/settings/anonymous-read` to `src/web/Api.cpp`; updated every existing read route to
  resolve an optional `Principal` and pass it through (**not yet compiled** — see "Known verification
  limitation" in `README.md`).
- Added `authorization_integration_tests` (SQLite): non-member vs. member vs. project-admin issue writes,
  not-found semantics under authorization, the anonymous-read-access toggle, and the full project
  lifecycle authorization matrix (project-admin vs. global-admin-only actions).
- Manually verified `createProject`, `setProjectArchived`, `softDeleteProject`, `listDeletedProjects`
  (`LATERAL` join + on-demand purge), `restoreProject`, `permanentlyDeleteProject`, and the
  `installation_settings` get/set-with-upsert methods against a live local PostgreSQL 16 server.

## Phase 1: identity and sessions (reduced scope)

- Re-reviewed all 142 original product decisions with the product owner and produced a reduced V1
  scope (`REDUCED_SCOPE_SPECIFICATION.md` and friends); this is now the build target instead of the
  original full-scope plan.
- Added local-account identity: `Principal`, Argon2id password hashing (`common/PasswordHash`), SHA-256
  session-token hashing (`common/Sha256`), and `AuthService` (login/logout/session validation,
  administrator-only account creation, minimal login-attempt lockout).
- Added migration `004_identity.sql` (both backends): `local_credentials`, `sessions`, and `users` gains
  `time_zone`/`clock_format`/`is_admin` while losing the prototype's `username` column. The SQLite
  variant rebuilds the `users` table (SQLite cannot `DROP COLUMN` a column in a `UNIQUE` constraint
  directly); `SqliteDatabase::migrate()` now disables and re-verifies foreign keys around every
  migration to support this and future table-rebuild migrations safely.
- Removed the fixed `demo` user from every write path. `TicketService::createIssue`/`changeStatus`/
  `addComment` now require an explicit `Domain::Principal` argument.
- Added `ticket-hub-cli create-user <email> <displayName> <password> [--admin]` — the entire V1
  registration/reset story (no public registration, no invitations, no forced password change).
- `CreateIssueRequest::assigneeUsername` renamed to `assigneeEmail` (assignee lookup is now by email,
  not the removed username column); updated in `web/index.html` and `web/app.js` accordingly.
- Added `/api/auth/login`, `/api/auth/logout`, `/api/auth/me`, and session-cookie + double-submit-CSRF
  protection to the existing write routes in `src/web/Api.cpp` (**not yet compiled** — see
  "Known verification limitation" in `README.md`).
- Added `crypto_tests` (SHA-256 known-answer vectors, Argon2id round-trip) and
  `identity_integration_tests` (SQLite: create-user, login success/failure, anti-enumeration, lockout,
  session lifecycle) test binaries; all previously-passing tests continue to pass.
- Manually verified migrations, seeding, `create-user`, and a full login/session/logout cycle against a
  live local PostgreSQL 16 server, in addition to the automated SQLite test suite.

## 0.2.0 — product baseline and migration foundation

- Consolidated the approved Jira-like product specification.
- Added architecture, target data model and phased roadmap documents.
- Added ordered schema migration discovery and checksum verification.
- Added PostgreSQL advisory locking for migrations.
- Separated demo seed execution from schema migration history.
- Added issue version to support optimistic status-change conflicts.
- Added project/issue key-alias tables and alias lookup for issues.
- Added recycle-bin foundation columns for projects, issues, comments and attachments.
- Added normalized project/issue key and label handling.
- Fixed duplicate project rows in the SQLite adapter.
- Expanded migration, domain and SQLite integration tests.
