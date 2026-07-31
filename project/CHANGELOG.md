# Changelog

## Unreleased — Reorder, move, and bulk-action UI: the demo UI now covers every Phase 1-3 route

- **Reorder (D31)**: filtering the Issues table to a single project now sorts it by `rankOrder` and adds
  an Order column with move-up/move-down buttons per row, calling `POST /api/issues/{key}/reorder` with
  the correct adjacent-issue anchor for a one-position swap. Only shown with a single project selected,
  since `reorderIssue`'s anchor must be in the same project as the issue being moved. Filtering back to
  "All projects" hides the column.
- **Move (D37)**: the issue drawer now has a "Move to project" control (target-project select + button,
  hidden when there's nowhere to move to) calling `POST /api/issues/{key}/move`; on success the drawer
  reopens showing the issue under its new key.
- **Bulk actions (D36)**: the Issues table now has a checkbox per row and a bulk-action bar (shown once at
  least one row is checked) offering set-status, assign, add-label, and delete, each calling the matching
  `POST /api/issues/bulk/*` route and reporting `succeeded`/`failed` counts as a toast. Done-category
  statuses are deliberately excluded from the bulk status picker, since bulk status change can't supply a
  per-issue `resolution` and every one of those calls would otherwise 422.
- Found and fixed a real bug proactively (before it could surface in testing): the new row checkboxes and
  reorder buttons live inside the same `<tr>` that already has a click-to-open-drawer handler
  (`bindIssueLinks()`), so a naive implementation would have both opened the issue drawer *and* performed
  the intended action on every click. Fixed by calling `event.stopPropagation()` in the checkbox and
  move-up/move-down click handlers.
- Verified end-to-end with a headless browser (Playwright/Chromium): the Order column appears only with a
  single project selected and disappears when cleared; moving a row up swaps it with its neighbor and
  moving back down restores the original order; moving an issue to another project navigates the drawer to
  its new key; clicking a row checkbox does *not* open the drawer; bulk-adding a label to two selected
  issues reports "2 succeeded, 0 failed". Re-ran every prior UI batch's browser test to confirm no
  regression -- all still pass unchanged.
- This closes out `web/`'s coverage of every route added across Phases 1-3 of
  `docs/REDUCED_SCOPE_ROADMAP.md`: every write the API exposes now has a reachable control in the demo UI.

## Unreleased — Issue recycle bin UI

- Added a Delete button (🗑) to the issue drawer's actions row (`DELETE /api/issues/{key}`), closing the
  drawer and refreshing the current view on success. A non-member/insufficient-role click surfaces the
  server's 403 as a toast (verified: a TH member who isn't a project admin gets "Actor lacks the required
  role on project TH", and the drawer stays open since nothing actually happened).
- Added a recycle-bin toggle to the Issues view, symmetric to the project recycle bin added last batch:
  visible only to global administrators, swaps the filter bar and normal issue table for a
  `GET /api/issues/deleted` list with Restore and Delete-permanently buttons per row (`renderIssues()` now
  delegates to `renderIssuesView(showingDeleted)`, mirroring `renderProjectsView`'s pattern exactly).
  Deleted-issue rows are deliberately not clickable to open the drawer (a soft-deleted issue isn't found by
  ordinary `GET /api/issues/{key}` lookup).
- Verified end-to-end with a headless browser (Playwright/Chromium): deleting an issue via the drawer
  removes it from the active Issues table and closes the drawer; it appears in the recycle bin; restoring
  it returns it to the active table; deleting and permanently deleting it removes it from the recycle bin
  for good; a non-admin sees no recycle-bin toggle at all, and a role-insufficient delete attempt fails
  with the server's exact error surfaced as a toast rather than silently succeeding or crashing.

## Unreleased — Project-management UI: create, archive, and recycle bin

- Added a "New project" modal (`POST /api/projects`, global-admin-only server-side) and, on each project
  card in the Projects view, Archive/Unarchive (`PATCH .../archived`) and Delete-to-recycle-bin
  (`DELETE /api/projects/{key}`) buttons. A non-admin's click surfaces the server's 403 as a toast, same
  pattern as every other write in this app -- no client-side role gate duplicates the server's check.
- Added a project recycle-bin view, toggled from the Projects view (only shown to global administrators,
  `state.principal.isAdmin`, matching D88's admin-only restore/purge split): lists deleted projects with
  Restore and Delete-permanently buttons.
- Found and fixed a real bug via this batch's own browser testing, unrelated to project management
  specifically: `state` (current view, selected project, filters, cached project/issue lists) was never
  reset on logout. A second user logging into the same browser tab landed on whatever
  tab/project/filters the previous user last had open -- which could reference a project the new user has
  no access to, or one that had since been archived or deleted. `showLoginScreen()` (called on explicit
  logout and on any session-expiry 401) now resets all of `state` back to its initial shape, factored into
  a shared `initialState()` function used both for the module-level `const state` and for this reset.
- Verified end-to-end with a headless browser (Playwright/Chromium): creating a project and seeing it in
  the grid; a duplicate key rejected inline with the server's exact message; archiving a project drops it
  from the active grid and the sidebar shortcuts (D87); delete moves it to the recycle bin, restore brings
  it back to the active grid, and permanent delete removes it from the recycle bin too; a non-admin sees
  neither the recycle-bin toggle nor a working create-project action (403 surfaced as an inline error); and
  -- the state-reset fix above -- logging out of one account and into another lands cleanly on the
  Dashboard rather than hanging on whatever view the prior session was showing.

## Unreleased — Full edit, clone, links, and watch/vote UI in the issue drawer

- Added an actions row to the issue drawer: Watch/Unwatch toggle (shows count), Vote/Unvote toggle (shows
  count), Clone (navigates to the new cloned issue), and Edit (switches the drawer into an editable form
  for summary/description/priority/assignee/story points/due date/labels, with Save calling
  `PATCH /api/issues/{key}` and Cancel discarding the in-progress edit).
- Added a Links section to the drawer: lists every link touching the issue (correct outward/inward label,
  e.g. "blocks" vs. "is blocked by"), a form to add a new link (target key + fixed link-type catalog), and
  a remove button per link. Links are clickable to navigate directly to the linked issue.
- The drawer now also shows the issue's parent (when linked, clickable) -- previously invisible in the UI
  even though `parentIssueKey` was already part of the issue JSON.
- Found and fixed two real bugs via this batch's own browser testing:
  - `web/app.js`: watch/vote/clone/link mutations all correctly re-render the drawer from fresh server
    state, but a naive first attempt at wiring the "Add link" form's target-issue click handler would
    have needed the page-wide `bindIssueLinks()` helper, which (as already learned in the resolution/
    hierarchy-picker batch) re-registers duplicate listeners on background view elements -- avoided by
    scoping link-row click handlers to the freshly-rendered drawer only.
  - `web/styles.css`: a genuine CSS layout bug -- `.link-list` is a grid container, and CSS grid items get
    an implicit content-based minimum width unless overridden, so a long link row (`.link-row`) refused to
    shrink below its own content's intrinsic width and visually overflowed 80+px into the drawer's meta
    sidebar column, making the delete-link button in that overlap region unclickable (Playwright's own
    "element intercepts pointer events" check caught this; a real user would have hit the same dead
    click). Fixed with an explicit `min-width: 0` on `.link-row`. Verified with `getBoundingClientRect()`
    measurements before and after: the row's right edge moved from 1090px (82px past the sidebar's left
    edge at 1036px) to 1008px (clear of it).
- Verified end-to-end with a headless browser (Playwright/Chromium): watch/vote toggles flip and revert
  correctly with accurate counts; cloning navigates to the new issue and the clone's own Links section
  already shows the automatic `clones` link back to the original (D60); adding a `relates_to` link shows
  the correct target summary and label, is clickable to navigate, and is deletable from either linked
  issue's side (confirming links are genuinely bidirectional through the UI, not just the API); a full
  edit (summary/description/priority/assignee/story points/labels) saves correctly and an in-progress
  edit can be cancelled without persisting.

## Unreleased — Fixed two broken UI paths: completing an issue, and setting an Epic/parent on create

- **Resolution picker** (`web/app.js`, drawer): completing an issue via the UI previously called
  `PATCH .../status` without `resolution`, which the server correctly rejects with HTTP 422 (D68) --
  there was no way to actually complete an issue from the demo UI at all. Selecting a Done-category status
  now reveals an inline resolution picker (`fixed`/`done`/`wont-fix`/`duplicate`/`cannot-reproduce`) with
  Confirm/Cancel, and only calls the API once a resolution is chosen. The drawer now also shows the
  issue's current resolution (once set) and its parent issue (once linked, clickable to navigate to it).
- **Epic/parent picker on create** (`web/index.html`/`app.js`): the create-issue modal previously had no
  way to set `parentIssueKey` at all, and was missing "Sub-task" from the issue-type list entirely. Added
  both. The picker narrows its candidate list and label text to match the fixed hierarchy (D5, D29,
  D64-D66): hidden entirely for Epic (no parent allowed), "Parent (required)" filtered to Story/Task/Bug
  issues in the selected project for Sub-task, "Epic (optional)" filtered to Epic issues in the selected
  project otherwise. The server remains the actual source of truth for the rule; the picker only narrows
  the common case.
- Fixed a race condition caught while browser-testing this batch: the project and issue-type `<select>`
  change handlers both call the same async parent-options refresh, and rapid selection changes could
  interleave two in-flight requests and duplicate options in the list. Fixed with a monotonically
  increasing request-id guard that discards a stale response once a newer refresh has started.
- Verified end-to-end with a headless browser (Playwright/Chromium): creating an Epic, then a Story with
  that Epic as parent (drawer shows and links to the parent correctly); a Sub-task submitted without a
  parent shows the server's exact validation message inline; completing a fresh issue reveals the
  resolution picker, applies the chosen resolution, and displays it; reopening the completed issue clears
  the resolution and hides the picker for that direction.

## Unreleased — Minimal login screen for the demo UI

- Added a login screen to `web/index.html`/`app.js`/`styles.css`: silently probes `GET /api/auth/me` on
  load and shows a sign-in form instead of the app shell if unauthenticated; a successful
  `POST /api/auth/login` reveals the app shell and shows the signed-in user's name/email/initials in the
  sidebar footer, alongside a sign-out button (`POST /api/auth/logout`).
- `app.js`'s `api()` helper now attaches `X-CSRF-Token` (read from the `th_csrf` cookie) to every
  non-`GET` request automatically, and treats any `401` from any API call as a session expiry, redirecting
  back to the login screen. Previously the demo UI never sent a CSRF token at all, so every write would
  have failed once CSRF enforcement was reachable.
- Verified with a real headless browser (Playwright/Chromium against the pre-installed browser), not just
  `curl`: fresh-load login gate, successful login rendering every existing view, a CSRF-protected issue
  create and status change both succeeding through the browser's own `fetch`, sign-out clearing cookies
  and staying on the login screen across a reload, and a wrong password producing an inline error without
  ever revealing the app shell. Confirmed the session/CSRF cookies' `Secure` attribute does not block
  local `127.0.0.1` testing (browsers treat it as a trustworthy origin) while still requiring real TLS in
  production. Full detail in `docs/VERIFICATION.md`.
- Project-management, hierarchy/resolution, edit/link/clone/watch-vote/recycle-bin/bulk-action/
  reorder/move UI still do not exist in `web/` -- only login does; see `NEXT.md`.

## Unreleased — Server target verified end-to-end for the first time

- Outbound network access to `github.com` became reachable in this environment, so the Crow-based
  `ticket-hub` server target was built (`-DTICKETHUB_BUILD_SERVER=ON`) for the first time this session,
  after installing the one missing system dependency, standalone `asio` (`sudo apt-get install
  libasio-dev`, already listed in `README.md`'s apt line). `src/main.cpp`, `src/web/Api.cpp`, and
  `src/web/HttpServer.cpp` compiled with zero warnings/errors from Ticket Hub's own code.
- Ran a live HTTP smoke test against a running instance covering essentially every route across all three
  completed phases: login/logout/session validation, CSRF enforcement, project-role enforcement, the
  fixed workflow's resolution-required/cleared and optimistic-lock-conflict rules, full-replacement edit,
  cloning, issue links, watching/voting, the issue recycle bin, all four bulk actions, comments, the full
  project lifecycle, the anonymous-read-access toggle, and the newest `reorder`/`move` routes (confirming
  a moved issue's vacated key resolves via `issue_key_aliases` through the real HTTP/JSON layer). **Zero
  bugs found** in `Api.cpp` — every route, written blind against established patterns across the whole
  session up to this point, behaved exactly as documented on the first real test.
- This closes the standing cross-phase verification gap recorded in every prior entry of this changelog
  and `docs/VERIFICATION.md`. Full detail in `docs/VERIFICATION.md`'s "Server target verified end-to-end"
  entry. The one remaining gap is that `web/` still has no login page or Phase 2/3 UI — a feature gap, not
  a verification gap (see `NEXT.md`).

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
