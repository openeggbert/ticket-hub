# Ticket Hub next work

Current version: 0.2.0 (Phase 3 complete at every layer -- core, tests, server, and UI; Phase 4
(Collaboration) nearly done -- comment editing/tombstone delete, fixed emoji reactions, @mention
handles/the fixed in-app notification set, the Markdown editor toolbar/preview, and simplified worklogs
are done; only the audit log (D23) remains — see below)
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
- **Phase 4 started (comment editing and tombstone delete, D81/D82/D83), this batch:** migration
  `008_comment_editing.sql` adds `comments.edited_at` (both backends) -- the simplified V1 answer to
  D81 (a single "this was edited at X" timestamp, not a version-history table). `IDatabase::
  findCommentById`/`editComment`/`deleteComment` in both adapters -- `editComment` shares the same
  optimistic-locking contract as `editIssue` (`expectedVersion` -> `Domain::ConcurrencyConflict`, 409)
  and sets `edited_at` on success; `deleteComment` is a tombstone soft-delete via the same
  `deleted_at`/`deleted_by_user_id` columns issues/projects already use (D82) -- there is no separate
  admin recycle-bin API for comments, the row and its original body simply remain in the database,
  excluded from ordinary listing, queryable only directly. Matching `TicketService::editComment`/
  `deleteComment` with simplified permissions (D83): the comment's own author may always edit/delete
  it, otherwise the actor needs project-Admin-or-above on the comment's issue's project (or global
  admin) -- no separate edit-own/edit-all/delete-own/delete-all matrix. New `PATCH`/
  `DELETE /api/issues/{key}/comments/{id}` routes in `Api.cpp` follow the established auth/CSRF/
  error-mapping pattern exactly. `web/` gained Edit/Delete buttons on each comment (hidden client-side
  for non-author/non-global-admin actors -- a UI simplification, not the security boundary; the server
  enforces D83 independently and the authorization tests confirm it), an inline edit textarea with
  Save/Cancel, and an `(edited)` marker. Browser-verified with Playwright/Chromium: add/edit/cancel/
  delete a comment as the author, then confirmed a different non-admin user (`sam`) does not see
  Edit/Delete on another user's (`alex`'s) comment. New SQLite-integration and authorization-integration
  test coverage for `findCommentById`/`editComment`/`deleteComment` (success, version-increment,
  `edited_at` set, stale-version conflict, unknown-comment no-op, non-author-non-admin Forbidden,
  self-edit succeeds, global-admin can edit/delete any comment). Full detail in
  `docs/VERIFICATION.md`.
- **Phase 4 continued (fixed emoji reactions on comments, D84), this batch:** migration
  `009_comment_reactions.sql` adds `comment_reactions` (both backends) -- a three-column composite-key
  many-to-many table (`comment_id`, `user_id`, `reaction_key`) mirroring `issue_watchers`/`issue_votes`,
  with `reaction_key` constrained to a fixed eight-value set (`thumbs_up`, `thumbs_down`, `laugh`,
  `hooray`, `confused`, `heart`, `rocket`, `eyes` -- GitHub's own well-known reaction set, chosen as a
  conservative default since the decision register calls for "a fixed reaction set" without enumerating
  one). `IDatabase::addCommentReaction`/`removeCommentReaction`/`listCommentReactions` in both adapters,
  matching `TicketService` methods with the same self-service/no-project-role reasoning as watch/vote
  (D20/D79) -- an unknown issue, comment, or reaction key throws `std::invalid_argument` (matching
  `watchIssue`'s own unknown-issue behavior, not `editComment`/`deleteComment`'s nullopt/false
  convention); add/remove return `true` only when a row was actually inserted/removed (idempotent on a
  repeat call). New `GET /api/issues/{key}/comments/{id}/reactions` and
  `POST`/`DELETE /api/issues/{key}/comments/{id}/reactions/{key}` routes in `Api.cpp`. `web/` renders all
  eight reactions as small pill buttons under each comment with a live count, highlighting the ones the
  current viewer has added; clicking toggles react/un-react. Browser-verified with Playwright/Chromium
  across two users: reacting shows the button go active with a count, clicking again removes it, a
  second distinct reaction key can coexist, and -- switching users -- the count is shared while each
  user's own "active" highlight is independently correct (explicitly asserted, not assumed). New
  SQLite-integration, authorization-integration, and live-PostgreSQL test coverage. Full detail in
  `docs/VERIFICATION.md`.
- **Phase 4 continued (@mention handles and the fixed in-app notification set, D56/D80/D14), this
  batch:** migration `010_mentions_and_notifications.sql` adds `users.handle` (nullable, unique via a
  partial index -- SQLite's `ALTER TABLE ADD COLUMN` cannot itself carry `UNIQUE`) and `notifications`
  (`user_id`, `type` fixed to `assigned`/`mentioned`/`watched_comment`, `issue_id` nullable, `read_at` --
  the exact minimal shape in `docs/REDUCED_SCOPE_DATA_MODEL.md`). `ticket-hub-cli create-user` gained
  `--handle=<handle>`, validated/normalized the same way as email (lowercase, pre-checked for uniqueness
  in `AuthService::createUser` rather than relying on the DB constraint's error message); the three
  seeded demo users now have handles (`demo`/`alex`/`sam`). `IDatabase::findUserByHandle` and
  `createNotification`/`listNotifications`/`countUnreadNotifications`/`markNotificationRead`/
  `markAllNotificationsRead` in both adapters -- `listNotifications` resolves `issueKey`/`issueSummary`
  at read time via a join, since there is no stored message string. `TicketService::createIssue`/
  `editIssue` notify a newly-set or changed assignee (skipping self-assignment and a no-op re-save with
  the same assignee); `addComment` extracts every `@handle` token from the body once, at creation (not
  on every edit, to avoid re-notifying on every save of an already-mentioning comment), notifies each
  resolved user, and notifies every watcher of the issue except the comment's own author -- a recipient
  who is both mentioned and watching the same comment gets exactly one notification, the more specific
  reason (mentioned) winning over the generic one (watched), a deliberate simplification rather than a
  stored dedupe key. New `GET /api/users` (directory listing for @mention autocomplete, session-required
  even when anonymous read is on) and `GET /api/notifications[?unread=true]`,
  `GET /api/notifications/unread-count`, `POST /api/notifications/{id}/read`,
  `POST /api/notifications/read-all` routes. `web/` gained a notification bell with an unread-count badge
  in the top bar (opens a panel listing notifications, click-to-mark-read-and-open-issue, a "mark all
  read" button) and an @mention autocomplete dropdown under the comment textarea (both add and edit),
  backed by the cached `/api/users` directory fetched once in `loadBaseData()`. Browser-verified:
  assigning an issue to a second user shows them exactly one unread notification; typing `@sa` in the
  comment box shows a matching suggestion that inserts the full handle; mentioning a user in a comment
  notifies them with the correct issue reference; the notification panel/badge/mark-read/mark-all-read
  flow all work through the real HTTP layer; a full regression re-run of the reaction and comment-editing
  browser tests still pass unchanged. New SQLite-integration, authorization-integration (each notification
  type isolated in its own issue with an explicit `markAllNotificationsRead` reset between sub-tests, so
  no sub-test's leftover watcher state contaminates the next one's assertions), identity-integration
  (handle normalization/uniqueness/format validation), and live-PostgreSQL test coverage. Full detail in
  `docs/VERIFICATION.md`.
- **Phase 4 continued (Markdown editor toolbar, live preview, and sanitized rendering, D16), this
  batch:** `web/` gained `renderMarkdown`/`renderMarkdownInline`, a deliberately small Markdown-to-HTML
  subset (bold `**x**`, italic `*x*`, inline code, links, `#`/`##`/`###` headings, `-`/`*` and `1.`
  lists, `>` blockquotes, fenced code, `---` rules) applied to comment bodies and issue descriptions
  wherever they're displayed. Safe by construction, not by a separate sanitization pass: the raw text is
  HTML-escaped *first* (the same `escapeHtml` used everywhere else), and every transform after that only
  wraps the already-escaped text in a fixed, hardcoded set of tags, so user input can never introduce a
  real HTML tag or attribute. Link targets are restricted to `http(s)`/`mailto`; any other scheme is left
  as literal `[text](url)` text. Added `attachMarkdownToolbar` (Bold/Italic/Code/Link/Bulleted-list/
  Numbered-list/Quote buttons plus a live-preview toggle, pure `textarea.selectionStart`/`selectionEnd`
  manipulation, no `execCommand`/`contenteditable`) and wired it to all four Markdown-capable textareas:
  comment add, comment edit, issue description on create, issue description on edit. No schema or API
  change -- bodies are still stored/transmitted as raw Markdown text. Caught and fixed a real rendering
  bug during implementation: the first cut of the italic regex accepted `_..._` as well as `*...*`, which
  mishandled text containing two separate double-underscore identifiers (e.g. `__init__`-style names) --
  an underscore from the *first* pair and one from the *second* pair matched as open/close delimiters,
  silently swallowing everything between them into one (still safely escaped, just visually wrong) `<em>`
  span. Fixed by dropping underscore-delimited emphasis entirely (bold/italic use only `**`/`*`, which
  have no such adjacency ambiguity). Browser-verified: the toolbar's Bold button wraps a text selection
  with `**`; the Preview toggle shows/hides a live-rendered pane and swaps back correctly; a posted
  comment with bold/italic/code/link/list/quote markup renders as real `<strong>`/`<em>`/`<code>`/`<a>`/
  `<ul><li>`/`<blockquote>` elements; a `<script>`/`onerror`-`<img>` payload renders as inert literal text
  with no code execution (checked via a page-level flag, not just visual inspection); a
  `javascript:`-scheme link renders as literal bracket-paren text, never a clickable anchor; an issue
  description edit renders its heading/bold correctly. Re-ran the reaction, comment-editing, and
  mentions/notifications browser tests to confirm no regression from `.comment-body-text` changing from
  `<p>` to `<div>` (needed to legally contain the new block-level Markdown output). Full detail in
  `docs/VERIFICATION.md`.
- **Phase 4 continued (simplified worklogs, D12/D13), this batch:** migration `011_worklogs.sql` adds
  `worklogs` (`id`, `issue_id`, `author_user_id`, `work_date`, `time_spent_seconds`, `comment` nullable,
  plus the same tombstone-delete/`version` columns comments already use) -- no remaining-estimate
  linkage, since D12 dropped time estimates entirely, so there is nothing for a worklog to adjust.
  `IDatabase::listWorklogs`/`addWorklog`/`findWorklogById`/`editWorklog`/`deleteWorklog` in both
  adapters; `editWorklog` shares `editComment`/`editIssue`'s optimistic-locking contract. Matching
  `TicketService` methods deliberately drop D83's author-or-admin permission split: add/edit/delete all
  require only project-Member-or-above, so any project member may edit or delete *any* worklog on an
  issue they can access, not just their own (D13's explicit simplification). New
  `GET`/`POST /api/issues/{key}/worklogs` and `PATCH`/`DELETE /api/issues/{key}/worklogs/{id}` routes.
  `web/` gained a "Time tracking" section in the issue drawer: a list of logged entries with a Delete
  button on every one (shown unconditionally, no author check, matching the server's more permissive
  rule) and a log-time form accepting a free-text duration ("1h 30m", "45m") parsed client-side. New
  SQLite-integration (full CRUD, stale-version conflict, tombstone semantics), authorization-integration
  (a non-member rejected, a different project member editing/deleting someone else's worklog succeeds --
  the no-own-vs-others-split behavior explicitly asserted), and live-PostgreSQL test coverage. Full detail
  in `docs/VERIFICATION.md`. Only D23 (the append-only admin/security audit log) remains open in Phase 4
  -- see "Not yet built" in `docs/SCOPE.md`.

## `web/` UI now covers every Phase 1-3 route; Phase 4 (Collaboration) has started. Immediate next step: continue Phase 4

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
Comment editing/tombstone delete (D81/D82/D83), fixed emoji reactions (D84), @mention handles/the
fixed in-app notification set (D56/D80/D14), the Markdown editor toolbar/live preview (D16), and
simplified worklogs (D12/D13) are the first five Phase 4 slices, and all five are also already fully
covered in the UI. What's left is the rest of Phase 4, Phase 5, or optional UX polish:

1. Continue Phase 4 (Collaboration) per `docs/REDUCED_SCOPE_ROADMAP.md`: D23 (the append-only
   admin/security audit log), the last open item in Phase 4. Then Phase 5 (Attachments and Kanban
   board).
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
smoke test of essentially every route across all three completed phases and, across eleven batches, a
real-browser (Playwright/Chromium) test of every write route the demo UI now exposes: login/logout,
hierarchy/resolution pickers, full edit/clone/links/watch-vote/delete in the issue drawer, project
management, the issue recycle bin, reorder/move/bulk actions, comment editing/tombstone delete
(add/edit/cancel/delete as the author, plus a cross-user check that a non-author, non-admin user cannot
see Edit/Delete on someone else's comment), fixed emoji reactions (react/un-react toggling with a
live count, a second distinct reaction key coexisting with the first, and a cross-user check that counts
are shared while each user's own "active" highlight is independently correct), @mention handles
and the fixed in-app notification set (assigning notifies the assignee, @mention autocomplete inserts a
matching handle and notifies the mentioned user, and the notification panel/badge/mark-read/mark-all-read
flow), and now the Markdown editor toolbar and live preview (toolbar buttons producing correctly-rendered
`<strong>`/`<em>`/`<code>`/`<a>`/list/`<blockquote>` output, the preview toggle, and -- security-focused
-- confirming a `<script>`/`onerror`-`<img>` payload never executes and a `javascript:`-scheme link never
becomes clickable), and now simplified worklogs (logging time shows the correct formatted duration and
comment, deleting an entry removes it, an unparseable duration is rejected before it reaches the server).
The long-standing "server target unverified because `github.com` is unreachable" limitation recorded in
every prior session no longer applies in this environment, and there is no longer a gap between what the
API exposes for Phases 1-3 (plus the comment-editing, reactions, mentions/notifications, Markdown-
rendering, and worklog slices of Phase 4) and what the demo UI can reach.
`findCommentById`/`editComment`/`deleteComment` gained dedicated SQLite-integration coverage (success,
version-increment, `edited_at` set, stale-version conflict, unknown-comment no-op) and
authorization-integration coverage (non-author-non-admin Forbidden, self-edit succeeds, global-admin can
edit/delete any comment); `addCommentReaction`/`removeCommentReaction`/`listCommentReactions` gained the
same three-layer coverage (SQLite-integration idempotency/listing, authorization-integration no-project-
role/unknown-key/unknown-comment rejection, and a live-PostgreSQL smoke test); `findUserByHandle` and the
five `notifications` methods gained the same three-layer coverage plus identity-integration coverage for
handle normalization/uniqueness/format validation on `create-user`. Full detail, including exactly what
was exercised (and the several real bugs this browser testing and test-writing caught and fixed along the
way), is in `docs/VERIFICATION.md`.
