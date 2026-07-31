# Ticket Hub next work

Current version: 0.2.0 (Phase 3 complete at the core/CLI/test layer; server target built and
live-verified; `web/` demo UI now covers every Phase 1-3 route — see below)
Current roadmap: **reduced-scope V1** — see `REDUCED_SCOPE_SPECIFICATION.md` and
`docs/REDUCED_SCOPE_ROADMAP.md`. `SPECIFICATION.md` and `docs/ROADMAP.md` are kept as the long-term
aspirational baseline but are **not** the current build target.

Scope was re-reviewed decision-by-decision with the product owner on 2026-07-31 (142/142 decisions;
see `docs/REDUCED_SCOPE_DECISIONS.md` and `docs/REMOVED_AND_DEFERRED_FEATURES.md`). Do not implement
anything from the removed/deferred list without an explicit new product conversation.

## Completed so far

- Approved reduced-scope V1 specification, architecture, data model, roadmap, and effort estimate.
- Prior batch: ordered checksummed migrations with PostgreSQL advisory locking, separate demo seed,
  issue optimistic-lock version and HTTP conflict foundation, project/issue key alias and recycle-bin
  schema foundations, key/label normalization, administration CLI (version/diagnostics/migrate/
  seed-demo), domain/migration/SQLite integration tests, SQLite-only and PostgreSQL-only build
  verification.
- **Phase 1 (identity and sessions):** `Principal`, `AuthService` (login/logout/session validation,
  administrator-only `createUser`, minimal login-attempt lockout), Argon2id password hashing, SHA-256
  session-token hashing, migration `004_identity.sql` on both backends, `ticket-hub-cli create-user`.
  The fixed `demo` user is gone from every write path.
- **Phase 2 (authorization and projects):** fixed project roles (`Domain::ProjectRoleViewer`/`Member`/
  `Admin`, `Domain::projectRoleRank`) and a global-administrator bypass, enforced in `TicketService`;
  `createIssue`/`changeStatus`/`addComment` require project-Member-or-above. Project lifecycle
  (`createProject`/`setProjectArchived`/`deleteProject`/`restoreProject`/`listDeletedProjects`/
  `permanentlyDeleteProject`) with the fixed 90-day on-demand recycle-bin retention. Migration
  `005_authorization.sql` adds `installation_settings`. Installation-wide anonymous read-access toggle
  (D59, off by default) — every read use case takes `std::optional<Principal>`.
- **Phase 3, partial (fixed workflow and hierarchy):** the fixed Epic → Story/Task/Bug → Sub-task
  hierarchy (D5, D29, D64-D66), enforced by `TicketService::requireValidHierarchy` on issue creation
  (`CreateIssueRequest` gained `parentIssueKey`, now actually persisted to `issues.parent_issue_id`,
  which previously existed and was read but never written). The fixed workflow's hardcoded transition
  rules (D68-D70), enforced transactionally inside `IDatabase::changeIssueStatus` in both adapters (not
  the application layer -- these rules depend on current database state and must not race with a
  concurrent change): completing an issue requires a valid `resolution` and is rejected with the new
  `Domain::WorkflowViolation` (HTTP 422) while any sub-task is unfinished; reopening (leaving a
  Done-category status) always clears `resolution` and never touches child issues. `Domain::Issue`
  gained a `resolution` field.
- **Phase 3, partial continued (full issue edit), this batch:** `Domain::EditIssueRequest` and
  `IDatabase::editIssue`/`TicketService::editIssue` (D129) -- a full-replacement edit of summary,
  description, priority, assignee, story points, due date, and labels, sharing `changeIssueStatus`'s
  optimistic-locking contract (`expectedVersion` -> `Domain::ConcurrencyConflict`) and the same
  project-Member-or-above role requirement. One `issue_history` row per field that actually changed.
  Does not edit `issueTypeKey`/`parentIssueKey` -- re-typing/re-parenting is not yet implemented. Shared
  validation logic factored into `appendIssueContentErrors` (`Validation.cpp`) and `normalizeLabels`
  (`TicketService.cpp`) instead of duplicating it between create and edit.
- **Phase 3, partial continued (issue links and cloning), this batch:** the fixed issue-link catalog
  (D17) -- `Domain::isValidLinkType`/`linkTypeLabels` (`blocks`, `relates_to`, `duplicates`, `clones`),
  `IDatabase::createIssueLink`/`listIssueLinks`/`findIssueLinkById`/`deleteIssueLink` in both adapters,
  and matching `TicketService` methods requiring project-Member-or-above on **both** linked issues'
  projects. Simple field-copy cloning (D60) via `TicketService::cloneIssue`, composed from the existing
  `createIssue` + the new `createIssueLink` (summary/description/type/priority/labels copied, an
  automatic `clones` link created; assignee/story points/due date/attachments/sub-tasks/other links not
  copied; a cloned Sub-task keeps its original parent since it cannot exist without one).
- **Phase 3, partial continued (watchers and voting), this batch:** migration `006_collaboration.sql`
  adds `issue_watchers`/`issue_votes` (both backends). `IDatabase::watchIssue`/`unwatchIssue`/
  `listWatchers` and `voteIssue`/`unvoteIssue`/`listVoters` (D20, D79) in both adapters, and matching
  `TicketService` methods -- deliberately with **no project-role check**, the one exception among issue
  writes, since watch/vote are self-referential and Jira itself gates them by "browse" access rather
  than a write-capable role; any authenticated user may watch/vote on any issue. Idempotent: watching
  twice (or unwatching a non-watch) is a no-op, reported via the return value rather than an error.
- **Phase 3, partial continued (issue recycle bin and bulk actions):** `IDatabase::
  softDeleteIssue`/`restoreIssue`/`listDeletedIssues`/`permanentlyDeleteIssue` in both adapters (D22),
  mirroring the project recycle bin exactly (fixed 90-day on-demand retention, no background purge job).
  `TicketService::deleteIssue` requires project-Admin-or-above; restore/list/permanent-delete are
  global-administrator-only, the same split as D88. Simple bulk actions (D36): `Domain::
  BulkActionResult` and `TicketService::bulkChangeStatus`/`bulkAssign`/`bulkAddLabel`/`bulkDelete`, each
  looping over a list of issue keys and calling the matching single-issue operation independently per
  key -- no new database code, no cross-issue transaction, a partial failure reported via
  `succeeded`/`failed` key lists rather than rolled back.
- **Phase 3, complete at the core/CLI/test layer (manual ordering and moving between projects), this
  batch:** migration `007_ranking.sql` drops the never-used `issues.rank_value TEXT` LexoRank placeholder
  and adds `issues.rank_order INTEGER NOT NULL DEFAULT 0`. `IDatabase::reorderIssue` (D31): a full
  renumbering pass on every move (fetch the project's live issue-id list ordered by rank, remove the
  moving issue, re-insert it before a given anchor or append it, renumber the whole list `1..N`) --
  justified directly by D31's own "sufficient for small per-project issue counts" wording rather than a
  minimal-diff/fractional scheme. `IDatabase::moveIssue` (D37): moves an issue to a different project with
  no compatibility check needed (every project shares the same fixed types/workflow/fields) -- a
  `project_id` change plus a freshly allocated key/number in the target project, exactly like
  `createIssue`; rejected if the issue has a parent or any children (D64-D66 require them to share a
  project); the vacated key becomes a permanent alias (D38) via `issue_key_aliases`, the first code path
  that actually writes to that table. Matching `TicketService::reorderIssue` (project-Member-or-above on
  the issue's own project) and `TicketService::moveIssue` (project-Member-or-above on **both** the source
  and target projects, mirroring `createIssueLink`'s pattern). Caught and fixed a real migration-ordering
  bug during this batch: `002_seed_demo.sql` runs outside the checksummed migration flow and, in practice,
  after all schema migrations including `007_ranking.sql`, so its seeded issues were getting the column's
  `DEFAULT 0` instead of a backfilled rank -- fixed by setting `rank_order` explicitly in the seed
  `INSERT` itself. This closes out Phase 3's core-layer scope except for re-typing/re-parenting (see
  "Rest of Phase 3" below).
- Tested: `ctest --output-on-failure` is 7/7 green (`domain`, `migration`, `sqlite-integration`,
  `identity`, `authorization`, `workflow`, `crypto`) on SQLite, in all three build configurations (full,
  SQLite-only, PostgreSQL-only). Every Phase 2/3 core-layer addition was additionally verified manually
  against a live local PostgreSQL server (created and dropped for each batch's verification). Full
  detail in `docs/VERIFICATION.md`.
- Web layer source (`src/web/Api.cpp`, `HttpServer.cpp`, `main.cpp`) updated to match all phases so far:
  Phase 1's `/api/auth/login|logout|me` and session-cookie/CSRF protection, Phase 2's project CRUD
  routes and every read route resolving an optional `Principal`, Phase 3's `parentIssueKey` on issue
  creation, `resolution` on status changes, `Domain::WorkflowViolation` mapped to HTTP 422, the
  `PATCH /api/issues/{key}` full-edit route, `POST /api/issues/{key}/clone`,
  `GET`/`POST /api/issues/{key}/links`, `DELETE /api/issue-links/{id}`,
  `POST`/`DELETE /api/issues/{key}/watch`, `GET /api/issues/{key}/watchers`,
  `POST`/`DELETE /api/issues/{key}/vote`, `GET /api/issues/{key}/voters`,
  `DELETE /api/issues/{key}`, `GET /api/issues/deleted`, `POST /api/issues/{key}/restore`,
  `DELETE /api/issues/{key}/permanent`, `POST /api/issues/bulk/{status,assign,label,delete}`, and the new
  `POST /api/issues/{key}/reorder` and `POST /api/issues/{key}/move` routes (plus `rankOrder` added to the
  issue JSON representation). While adding the edit route (an earlier batch), fixed a real bug found by
  inspection: three existing routes (`POST /api/issues`, `PATCH /api/issues/{key}/status`,
  `POST /api/issues/{key}/comments`) were missing a `catch (const Domain::Forbidden&)` handler, so a
  project-role authorization failure would have fallen through to the generic 500 handler instead of 403.
- **Server target verified end-to-end, this batch:** outbound network access to `github.com` became
  reachable in this environment, so the Crow-based `ticket-hub` server target was built
  (`-DTICKETHUB_BUILD_SERVER=ON`) for the first time this session, after installing the one missing system
  dependency (`sudo apt-get install libasio-dev` — standalone `asio`, which Crow 1.3.3 requires and which
  was already listed in `README.md`'s apt line but not yet installed in this sandbox). `src/main.cpp`,
  `src/web/Api.cpp`, and `src/web/HttpServer.cpp` compiled with zero warnings/errors from Ticket Hub's own
  code (Crow's own headers emit expected third-party `-Wconversion` noise). Then ran a live HTTP smoke
  test covering essentially every route in every phase — login/logout/CSRF/session, project-role
  enforcement, the fixed workflow's resolution/409-conflict rules, full edit, links, cloning,
  watch/vote, the recycle bin, all four bulk actions, comments, the full project lifecycle, the
  anonymous-read-access toggle, and both new `reorder`/`move` routes (confirming the moved issue's
  vacated key still resolves via `issue_key_aliases` through the real HTTP/JSON layer, not just the
  database layer). **Zero bugs found** in `Api.cpp` — every route worked exactly as documented on the
  first real test, despite being written blind against established patterns for the whole session up to
  this point. Full detail in `docs/VERIFICATION.md`'s "Server target verified end-to-end" entry and
  `README.md`'s "Server verification" section. This closes the one standing cross-phase verification gap
  that every prior batch's report had to caveat.

## `web/` UI now covers every Phase 1-3 route. Immediate next step: continue the roadmap (Phase 4/5) or add UX polish

Across six batches, `web/` grew from a read-only demo (no auth, no writes reachable except create-issue
and status-change) into full coverage of every route the API exposes: login; an Epic/parent picker on
create; an inline resolution picker on status change; full edit/clone/links/watch-vote and delete in the
issue drawer; project management (create, archive/unarchive, recycle bin); the issue recycle bin
(symmetric to the project one); and, this final batch, manual reordering (an Order column with up/down
buttons, shown only with a single project selected), moving an issue to another project (a picker in the
drawer), and simple bulk actions (checkboxes plus a bulk-action bar for status/assign/label/delete).
Every one of these was verified end-to-end with a real headless browser (Playwright/Chromium), not just
`curl` -- full detail in `docs/VERIFICATION.md`. That testing caught and fixed several real bugs along the
way: a CSS layout bug (a link row overflowing into the drawer's sidebar and blocking clicks), a
state-management bug (`state` was never reset on logout, so a second user in the same browser tab could
land on a project they can't access or one the first user just archived/deleted), and -- caught
proactively during implementation, before it could surface as a failing test -- a click-bubbling issue
where the new checkboxes/reorder buttons live inside the same table row that already opens the issue
drawer on click.

There is no remaining gap between what the API exposes (for Phases 1-3) and what the demo UI can reach.
What's left is roadmap continuation or optional UX polish, not missing functionality:

1. Continue the roadmap into Phase 4 (Collaboration) / Phase 5 (Attachments and Kanban board) per
   `docs/REDUCED_SCOPE_ROADMAP.md` -- the natural next step now that Phase 1-3 is complete end-to-end
   (core, tests, server, and UI).
2. Optional UX polish that was never part of the write-route coverage goal: drag-and-drop reordering on
   the Board view (today's board is read-only, clicking a card just opens the drawer; the Issues table's
   up/down buttons are the only reorder UI); a friendlier bulk-status picker that also supports
   Done-category statuses by prompting for a shared resolution; keyboard-driven multi-select.
3. Re-typing (`issueTypeKey`) or re-parenting (`parentIssueKey`) an issue after creation is still
   deliberately unimplemented at the application/database layer (see "Finish Phase 3" above) -- no UI
   would have anywhere to call into for this even if it existed.

## Finish Phase 3, then continue the roadmap

Phase 3's core/CLI/test layer is now complete except for one item:

- Re-typing (`issueTypeKey`) or re-parenting (`parentIssueKey`) an issue after creation --
  `TicketService::editIssue` deliberately does not touch either field yet. `moveIssue` (D37) exists but
  deliberately does not re-parent or un-parent -- it rejects moving an issue that currently has a parent
  or any children, so this remains the one open path.

After Phase 3 is fully closed, continue with Milestone 2 (collaboration, attachments, Kanban board),
Milestone 3 (API, backup/restore), Milestone 4 (packaging and hardening). Do not jump ahead to
later-phase features early, and do not implement anything from `docs/REMOVED_AND_DEFERRED_FEATURES.md`.

## Verification status

Core, CLI, all seven test binaries, and the `ticket-hub` server target itself all compile and pass/run
cleanly on both SQLite and PostgreSQL, in every supported build configuration, including a live HTTP
smoke test of essentially every route across all three completed phases and, across six batches, a
real-browser (Playwright/Chromium) test of every write route the demo UI now exposes: login/logout,
hierarchy/resolution pickers, full edit/clone/links/watch-vote/delete in the issue drawer, project
management, the issue recycle bin, and reorder/move/bulk actions. The long-standing "server target
unverified because `github.com` is unreachable" limitation recorded in every prior session no longer
applies in this environment, and there is no longer a gap between what the API exposes for Phases 1-3
and what the demo UI can reach. Full detail, including exactly what was exercised (and the several real
bugs this browser testing caught and fixed along the way), is in `docs/VERIFICATION.md`.
