# Ticket Hub next work

Current version: 0.2.0. Phases 1-5 (Milestones 1-2) are complete at every layer (core, tests, server, and
UI) -- see below for detail. **Milestone 3 (Phases 6 and 7) is now fully complete**: the entire REST API
hardening/export list (PATs, active sessions, rate limits, `/api/v1` versioning, CSV export, request/
batch-size constants, a security hardening pass that found and fixed a real stored-XSS vulnerability, and
partial pagination for `GET /api/v1/issues`) and the entire backup/restore/upgrade/observability list
(backup/restore live-verified on both databases, the existing `migrate` command confirmed to satisfy the
upgrade requirement, structured JSON logs to stdout, and an in-app admin version banner). Full
batch-by-batch detail below.

**Milestone 4 (Phase 8, packaging and release hardening) is now fully complete, which closes the entire
reduced-scope V1 roadmap:** the Docker image and Compose distribution path (D50) -- `docker compose up`
brings up the full instance, verified as far as this environment's network policy allows (see
`docs/VERIFICATION.md` for the exact disclosure); light and dark theme (D46) -- the UI follows the
OS-level `prefers-color-scheme` signal automatically, browser-verified in both modes; the accessibility
baseline pass (D47) and browser-support note (D139) -- keyboard operability fixed for issue rows/board
cards/project cards/inline key links, D139 reconfirmed satisfied by construction; and the threat-model/
security self-review (`docs/THREAT_MODEL.md`), which found and fixed a real broken-access-control (IDOR)
bug plus four lower-severity issues (an unnecessarily session-derived CSRF cookie, a login timing side
channel, a missing CSRF check on logout, and CSV formula injection) -- see the "Threat-model/security
self-review" batch entry below for full detail. See "The roadmap is now complete" near the end of this
file for what remains only as optional, non-roadmap follow-up.

**Post-V1, batch 1 (done):** a web UI for managing personal access tokens and active sessions -- the
first optional item, picked by explicit user choice; see "The roadmap is now complete" below for detail.

**Post-V1, batch 2 (done):** re-typing (`issueTypeKey`) and re-parenting (`parentIssueKey`) an issue after
creation -- the one deliberate gap left open since Phase 3, closed as the second optional item, again
picked by explicit user choice; see "The roadmap is now complete" below for detail.

**Post-V1, batch 3 (done):** drag-and-drop card movement on the Kanban board -- the third optional item,
again picked by explicit user choice; see "The roadmap is now complete" below for detail.

**Post-V1, batch 4 (done):** the bulk Done-status picker (with a shared resolution prompt) and keyboard-
driven multi-select for the issues table -- the last two items on the optional-follow-up list, both picked
together in one go; see "The roadmap is now complete" below for detail.

**Post-V1, batch 5 (done):** Jira-style `/browse/{key}` direct issue links with full browser history
support (Back/Forward, deep links, reload-safe) -- a new user-requested addition after the original
optional-follow-up list (batches 1-4) had already been fully closed out; see "The roadmap is now complete"
below for detail.

**Post-V1, batch 6 (done):** a full "issue" -> "ticket" terminology rename across the database schema,
REST API, C++ code, and UI, to match the product's own name (Ticket Hub) -- another new user-requested
addition; see "The roadmap is now complete" below for detail.

**Post-V1, batch 7 (done):** project components (D19, `KEEP_FOR_V1`) -- discovered, while answering a user
question about ticket fields, to be the one V1-decided feature that was never actually implemented; see
"The roadmap is now complete" below for detail.

**Post-V1, batch 8 (done, 2026-08-03):** a full re-audit of every `KEEP_FOR_V1`/
`ALREADY_IMPLEMENTED_AND_KEEP` decision in `docs/REDUCED_SCOPE_DECISIONS.md` against the actual codebase
found five more decided features that were never actually implemented -- Markdown checklist rendering
(D62), a "No Epic" ticket filter (D66), a conflict dialog on a stale optimistic-lock save (D129),
self-service timezone/clock-format preferences (D45), and changing an active project's key (D91) -- all
five implemented, tested, and verified in one batch; see `docs/SCOPE.md`'s "Batch 8" entry and
"The roadmap is now complete" below for full detail.

**Post-V1, batch 9 (done, 2026-08-03):** three more user-requested changes in one pass -- a project's
backlog can grow far past what a Kanban column usefully displays, so Backlog is off the board (amending
D32) and has its own dedicated, paginated screen instead; worklogs are now Markdown-supported instead of
forced single-line; and every ticket list table now shows Created/Updated columns, matching what the
ticket detail drawer already showed. See `docs/SCOPE.md`'s "Batch 9" entry and "The roadmap is now
complete" below for full detail.

**Post-V1, batch 10 (done, 2026-08-03):** "prosim at je layout detailu ticketu vice podobny jire"
("please make the ticket detail layout more similar to Jira"). Restyled the ticket drawer: a colored
status pill near the title (instead of a plain select in the sidebar), two bordered "Details"/"Dates"
sidebar panel cards, and Comments/Work log as Activity tabs instead of two always-visible sections. Found
and fixed a real pre-existing backend bug along the way (a same-status call could silently drop a
resolution being confirmed on a Done ticket that somehow had none) plus a CSS regression the redesign
itself introduced (`[hidden]` being overridden by a same-specificity `display: flex` rule). See
`docs/SCOPE.md`'s "Batch 10" entry and "The roadmap is now complete" below for full detail.

**Post-V1, batch 11 (done, 2026-08-03):** "a co pridat i zalozky history activity transitions ???" ("and
what about also adding History/Activity/Transitions tabs?"). `ticket_history` already existed and was
already written to on every status change and full edit, but had no read-side API -- exposed for the
first time via a new `IDatabase::listTicketHistory`/`GET /api/v1/tickets/{key}/history`, and surfaced in
the drawer as a third "History" Activity tab next to Comments and Work log, rendering each row as a
Jira-style "changed X from Y to Z" sentence with humanized labels. No schema change -- purely new read
exposure of an existing table. See `docs/SCOPE.md`'s "Batch 11" entry and "The roadmap is now complete"
below for full detail.

**Post-V1, batch 12 (done, 2026-08-03):** "napis mi seznam moznych novych funkcionalit a ja se rozhodnu"
("write me a list of possible new functionalities and I'll decide") -- offered a menu of possible
additions; the user picked six ("implementuj prosim 1 3 4 6 11 12"). Three were quick, safe UI/pagination
additions, done in this batch: quick filters on Board/Backlog (one-click "Only my tickets"/"No Epic"
toggles), more keyboard shortcuts (`/` search, `?` help modal, arrow-key row/card navigation), and D126
pagination extended to notifications and the admin audit log (scoped down from the original four-endpoint
ask -- comments/worklogs stay unpaginated since a single ticket's list is naturally bounded, unlike a
per-user notification list or the installation-wide audit log). The other three -- custom fields (D9),
outbound webhooks (D39/D41), outbound email (D52) -- are real decision-register-deferred features, tracked
separately in `docs/SCOPE.md`'s "Deferred after V1, in progress" note rather than as a single "Batch 13,"
since each needs its own migrations/tests/verification pass; webhooks and email additionally need a
durable outbox/delivery mechanism first per `CLAUDE.md`'s "no detached in-memory tasks for email/webhooks"
rule. See `docs/SCOPE.md`'s "Batch 12" entry and "The roadmap is now complete" below for full detail.

**Post-V1, batch 13 (done, 2026-08-03):** custom fields (D9, deferred-after-V1) -- item 6 from the same
user-picked menu as batch 12. Admin-defined fields (text/number/date/checkbox/single-select/multi-select)
scoped to a single project, shown on ticket create/edit/view; a project's Admin-or-above manages the field
catalog, and a `required` field blocks a ticket create/edit that omits it. Deliberately scoped down from
D9's full target (one context per field, no per-stage visibility flags, no default value) -- see
`docs/SCOPE.md`'s "Batch 13" entry for exactly what was cut and why. Found and fixed two real pre-existing
bugs along the way, unrelated to custom fields themselves: a nondeterministic SQLite integration test
(likely the actual cause of "transient" failures noted in earlier verification passes) and a CSS overflow
on the Projects screen's action-button row. See `docs/SCOPE.md`'s "Batch 13" entry and "The roadmap is now
complete" below for full detail.

**Post-V1, batch 14 (done, 2026-08-03):** outbound webhooks (D39/D41) and outbound email (D52) --
items 11 and 12, the last two from the same user-picked menu as batches 12/13. Both needed a durable
delivery mechanism first (`CLAUDE.md`: no detached in-memory tasks for email/webhooks): a new
`webhook_deliveries`/`email_deliveries` outbox and a `ticket-hub-cli process-outbox` command, the only
place in the app that ever makes an outbound network call. Found and fixed a real bug during this batch's
own live-delivery verification: a mixed anonymous/numbered SQLite bind-index mistake was writing a failed
delivery's own id into its `last_error` column instead of the actual error message. See `docs/SCOPE.md`'s
"Batch 14" entry and the detailed section below for full detail.

**Post-V1, batch 15 (done, 2026-08-03):** REST write idempotency keys (D128, deferred-after-V1) --
requested from a follow-up menu offered after batch 14 closed the original six-item list; the user picked
exactly this one item ("pouze 1"). An optional `Idempotency-Key` header on the five POST routes that
create a new, independently visible resource (ticket, project, comment, worklog, ticket clone) -- a
retried request with the same key and body replays the original response instead of creating a duplicate;
the same key reused with a genuinely different body gets a 409, not a silently wrong replay. New
`idempotency_keys` table (migration `020_idempotency_keys.sql`). The demo web UI's own five equivalent
forms/buttons now send this header too, and disable their submit control for the duration of the request
-- the button-disable is what actually stops a literal double-click (a fresh key alone can't, since one is
minted per handler call), while the key covers the case a double-click guard can't: a successful response
that never reaches the browser, followed by a manual retry. Found and fixed a real pre-existing bug while
wiring the frontend: `api()`'s `fetch(path, { headers, ...options })` let `options.headers` silently
clobber the function's own merged `Content-Type`/`X-CSRF-Token` headers whenever a caller passed one --
invisible until this batch became the first caller ever to do so, at which point every wired action
started failing with a spurious 403. See `docs/SCOPE.md`'s "Batch 15" entry and the detailed section below
for full detail.

**Post-V1, batch 16 (done, 2026-08-05):** bug fix, user-reported -- archiving a project made it disappear
from the web UI with no way back (the recycle bin only holds soft-deleted projects, and `GET
/api/v1/projects` excludes archived ones by design, D87). Added `listArchivedProjects` (`IDatabase`/
`TicketService`/`GET /api/v1/projects/archived`, same read-access rule as the active list, not
admin-gated like the recycle bin) and fixed `projectJson()` never serializing `archived` at all -- the
existing per-card Unarchive button was dead code as a result. `web/`'s Projects page gained an "📦
Archived" toggle with a working Unarchive button. See "The roadmap is now complete" below for detail.

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
  Does not edit `issueTypeKey`/`parentIssueKey` at this point in the project -- re-typing/re-parenting was
  added later, as the second batch of post-V1 optional follow-up (see "The roadmap is now complete" near
  the end of this file). Shared
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
  `INSERT` itself. This closed out Phase 3's core-layer scope at the time except for re-typing/re-parenting,
  which was added later as post-V1 optional follow-up (see "The roadmap is now complete" below).
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
  in `docs/VERIFICATION.md`.
- **Phase 4 complete (simple append-only admin/security audit log, D23), this batch:** migration
  `012_audit_log.sql` adds `audit_events` (`id`, `category`, `action`, `actor_user_id` nullable,
  `target_type`/`target_id` nullable, `details` nullable, `created_at`) -- no categories-as-a-retention-
  feature, export, or configurable retention; rows are simply appended and never updated or purged.
  `IDatabase::recordAuditEvent` (fire-and-forget, `void`) and `listAuditEvents(limit)` (newest-first, no
  pagination/filtering) in both adapters. Rather than a general-purpose audit hook on every write, a
  small, deliberately focused set of existing call sites record an event as a side effect:
  `AuthService::login` on a wrong password (`auth`/`login.failed`) or an attempt against an
  already-locked account (`auth`/`login.blocked`), `AuthService::createUser` (`identity`/`user.created`,
  no actor since the CLI runs outside any web session), and `TicketService::setAnonymousReadEnabled`/
  `permanentlyDeleteProject`/`permanentlyDeleteIssue` (all `admin`-category). New
  `GET /api/admin/audit-events` route and `TicketService::listAuditEvents`, both
  global-administrator-only (same level as the recycle bins). `web/` gained an "Audit log" nav item
  (hidden for non-admins, and re-hidden on logout to avoid leaking it to the next user in the same
  browser tab) rendering a simple read-only table. Browser-verified: the nav item's visibility is
  correctly gated by admin status; toggling a setting produces matching rows in the log with the correct
  actor. New SQLite-integration, identity-integration (login-failed/login-blocked/user-created events
  recorded correctly), authorization-integration (global-admin-only read access; all three admin actions
  produce the expected events attributed to the correct actor), and live-PostgreSQL test coverage
  (including confirming the CLI's `create-user` produces a real audit row end-to-end through the actual
  binary, not just a direct database call). Full detail in `docs/VERIFICATION.md`. **This closes out
  Phase 4 (Collaboration) -- every item in `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 4 list is now
  implemented.**
- **Phase 5 started (ad-hoc issue filter/search widening, D10/D43), this batch:** `Domain::IssueFilter`
  gains `issueTypeKey`/`priorityKey`/`assigneeEmail`/`label`/`dueBefore` (all optional), alongside the
  pre-existing `projectKey`/`statusKey`/`search`. `SqliteDatabase::listIssues`/`PostgresDatabase::
  listIssues` both widened to the same 8-parameter `WHERE` clause: type/priority/assignee are equality
  joins against already-present query aliases, `dueBefore` is an inclusive `<=`, `label` is a fresh
  `EXISTS` subquery (so it narrows matches without dropping any of a matching issue's *other* labels from
  the already-aggregated label-list column), and `search` now also matches `i.description`, not just
  summary/issue key, per D43's plain-substring-match, no-full-text-index scope. `GET /api/issues` accepts
  matching `type`/`priority`/`assignee`/`label`/`dueBefore` query parameters (`TicketService::listIssues`
  needed no change, it already passed the filter through). `web/`'s Issues view filter bar gained
  type/priority/assignee dropdowns, a label input, and a due-date picker; "Clear" and the Board/
  global-search transitions all reset the new fields too, so a lingering ad-hoc filter can't leak between
  views. New SQLite-integration test coverage for every new field individually, a combined multi-field
  filter, the inclusive `dueBefore` boundary, and an explicit check that filtering by label does not
  corrupt the filtered issue's own label list. Live-PostgreSQL and browser-verified (Playwright/Chromium)
  the same cases, plus a full regression re-run of the markdown/mentions/reactions/worklog/audit-log/
  comment-editing browser tests. Full detail in `docs/VERIFICATION.md`.
- **Phase 5 continued (personal dashboard widgets, D24), this batch:** `Domain::DashboardStats` gains
  `assignedToMe`/`watchedIssues`/`upcomingDeadlines`, matching D24's fixed widget set (assigned issues,
  watched issues, recent activity, deadlines, simple stats -- no active-sprint widget, since Scrum was
  removed for V1). New `IDatabase::listWatchedIssues(userId, limit)` in both adapters, the reverse
  direction of the existing `listWatchers`. `TicketService::dashboard` personalizes for an authenticated
  actor: `assignedToMe` reuses the existing `listIssues` assignee filter and excludes Done-category
  issues; `upcomingDeadlines` is derived from that same result set app-side (no second query); all three
  stay empty for an anonymous viewer. `GET /api/dashboard` gains the three new arrays. `web/`'s Dashboard
  view gained three new panels (two via the existing `tablePanel` helper, one new `deadlinesPanel` helper
  with a Due date column), shown only when a principal is present. New SQLite-integration coverage for
  `listWatchedIssues` and authorization-integration coverage for `TicketService::dashboard`'s
  personalization (anonymous gets empty widgets, Done-category issues excluded from assigned-to-me,
  watched-issues reflects a fresh watch). Live-PostgreSQL and browser-verified (Playwright/Chromium: a
  real Watch-button click populates the watching widget, a real due-date edit populates the deadlines
  widget, and two different users see their own personalized widgets, not each other's), plus a full
  regression re-run of prior batches' browser tests. Full detail in `docs/VERIFICATION.md`.
- **Phase 5 continued (Kanban board WIP limits, D32/D33), this batch:** migration `013_board_columns.sql`
  adds `board_columns` -- a single flat, installation-wide table (one row per fixed workflow status, no
  `board_id`/`project_id` column at all), matching D32's "one board column equals one workflow status" and
  the reduced-scope data model's literal target schema. A WIP limit therefore applies to that status's
  column on every project's board, not per-project. `002_seed_demo.sql` seeds the five rows ("In
  Progress" given a demo limit of 3, the rest unlimited). New `Domain::BoardColumn`;
  `IDatabase::listBoardColumns()`/`setBoardColumnWipLimit(statusKey, optional<int>)` in both adapters;
  matching `TicketService` methods (read same as projects/issues, set is global-administrator-only, an
  unknown status key throws `std::invalid_argument`). New `GET /api/board-columns` and
  `PUT /api/board-columns/{statusKey}` routes. `web/`'s Board view shows each column's live count as
  `N / limit` (or plain `N` when unlimited) with a soft, display-time-only `.over-limit` highlight; global
  admins additionally get a small inline WIP-limit editor per column. Drag-and-drop board reordering was
  deliberately left out -- neither D32 nor D33 mentions it, and the roadmap's "board usable end-to-end"
  exit gate was already satisfied by the pre-existing click-to-drawer status change. New
  SQLite-integration and authorization-integration test coverage. Live-PostgreSQL and browser-verified
  (Playwright/Chromium: non-admin sees counts only, admin sees and can use the editor, an over-limit
  column highlights and clears, and the setting is confirmed genuinely installation-wide by checking a
  second project), plus a full regression re-run of prior batches' browser tests. Full detail in
  `docs/VERIFICATION.md`.
- **Phase 5 complete (attachments, D15/D98-D105), this batch:** the full attachments vertical, closing
  out Phase 5 and Milestone 2. `attachments.sha256`/`deleted_at`/`deleted_by_user_id` already existed
  (pre-provisioned in `003_product_foundation.sql`); migration `014_attachments.sql` adds only the
  `issue_id` index that table never got. New `Domain::Attachment`; `IDatabase::createAttachment` is the
  one create* method that takes a caller-supplied id, since the local filesystem storage key (D15,
  hardwired, no abstraction) must be known and the file already written before the row is inserted --
  `listAttachments`/`findAttachmentById`/`softDeleteAttachment`/`restoreAttachment`/
  `listDeletedAttachments`/`permanentlyDeleteAttachment` in both adapters mirror the existing tombstone
  pattern; `listAttachmentStorageKeysForIssue`/`...ForProject` return every attachment's storage key
  regardless of soft-delete state, used to delete files on disk before a permanent issue/project delete
  cascades through the database (D105 has no periodic orphan-file audit at all). New
  `src/infrastructure/storage/LocalAttachmentStorage` (plain, non-virtual -- D15's "no abstract storage
  port"), keyed by the attachment's own UUID, rooted at `TICKETHUB_ATTACHMENTS_DIR`.
  `Domain::validateAttachmentUpload` enforces D98's fixed 25MB/file, 20-attachments/issue, and a blocked-
  extension denylist. `TicketService::uploadAttachment` computes the SHA-256 at upload time (D105);
  `deleteAttachment` is uploader-or-project-Admin-or-above (mirroring D83's comment rule, the closest
  precedent -- no decision addresses this directly); `listDeletedAttachments` implements D102's fixed
  90-day on-demand retention itself, one layer above the SQL adapter, since purging an attachment also
  means deleting its file. New `GET`/`POST /api/issues/{key}/attachments`,
  `DELETE /api/issues/{key}/attachments/{id}`, `GET /api/attachments/{id}/download` (not nested under
  `/issues/{key}`, since a download/preview URL only ever needs the id), and the recycle-bin routes
  (`GET /api/attachments/deleted`, `POST .../restore`, `DELETE .../permanent`, all global-admin-only).
  `web/`'s issue drawer gained a sortable Attachments section (D101: name/size/date/uploader/type),
  drag-and-drop upload, and native-element previews for all four D99 kinds (image/PDF/text/audio-video).
  The Markdown toolbar gained full upload + drag/drop + paste (D100), inserting
  `![name](attachment://id)`/`[name](attachment://id)`; `renderMarkdownInline` gained real image-syntax
  support (previously absent) and resolves `attachment://<id>` to a real download URL, validating the id
  shape first and leaving anything malformed as inert text. A new admin-only "Attachment recycle bin" nav
  item mirrors the audit log's visibility pattern. New SQLite-integration and authorization-integration
  test coverage (full CRUD, both permission rules, the fixed limits, the recycle-bin split). Live-
  PostgreSQL and extensively browser-verified (upload/preview/sort/delete, all four preview kinds, real
  native drag-and-drop and clipboard paste -- not just `setInputFiles` -- into both the dropzone and the
  Markdown editor, an inserted `attachment://` reference actually resolving when a comment is rendered,
  and the full recycle-bin flow), plus a full regression re-run of every prior batch's browser tests. Two
  real bugs were caught and fixed before this could be considered complete: a Postgres-only "inconsistent
  types deduced for $1" error from reusing one placeholder for two differently-typed columns, and a
  redundant migration that tried to re-add three columns the schema already had (caught immediately by
  `ctest`, never shipped). Full detail in `docs/VERIFICATION.md`.
- **Phase 6 started (personal access tokens, D39/D40), this batch:** migration
  `015_personal_access_tokens.sql` adds `personal_access_tokens`, mirroring `sessions` plus `name`,
  `last_used_at`, and `revoked_at`. New `Domain::PersonalAccessToken`/`CreatedPersonalAccessToken` (the
  raw token is returned only once, at creation); `IDatabase::createPersonalAccessToken`/
  `findPersonalAccessTokenByHash`/`listPersonalAccessTokens`/`revokePersonalAccessToken`/
  `touchPersonalAccessTokenLastUsed` in both adapters, mirroring the existing session methods exactly.
  `AuthService` gained matching self-service methods (`revokePersonalAccessToken` is ownership-scoped).
  `Api.cpp`'s `resolvePrincipal` now also accepts an `Authorization: Bearer <token>` header when no
  session cookie is present (D54: cookie and Bearer auth are mutually exclusive per request); `
  csrfTokenValid` now exempts any request with no session cookie in play, since CSRF only defends against
  a browser silently attaching a cookie -- this required zero changes to the ~50 existing route handlers.
  New self-service `GET`/`POST /api/tokens` and `DELETE /api/tokens/{id}` routes. New
  identity-integration test coverage (create/validate/list/revoke, last-used tracking, ownership
  enforcement, the fixed-expiration requirement). Live-PostgreSQL verified directly, and end-to-end via
  `curl` against a running server: created a token via cookie auth, used it as a Bearer header with no
  cookies at all to both read and **write** with no CSRF header (confirming the exemption works through
  the real HTTP layer), confirmed `lastUsedAt` updates, and confirmed a revoked token gets a 401. No web
  UI yet for managing tokens -- `/api/tokens` is fully functional but reachable only via `curl`/scripts.
  Full detail in `docs/VERIFICATION.md`.
- **Phase 6 continued (active-session list and "sign out everywhere", D54), this batch:** no new
  migration -- `sessions` already had everything needed. New `IDatabase::listSessionsForUser(userId)`
  and `deleteOtherSessionsForUser(userId, keepSessionId)` in both adapters. New
  `AuthService::currentSession(sessionToken)` resolves the session row itself (not just the `Principal`),
  so a caller can identify which listed session is "this one"; `listActiveSessions`/
  `signOutOtherSessions` are thin wrappers. Conservative default, since no decision text specifies it:
  "sign out everywhere" keeps the caller's own current session active and only removes the others
  (matching the common GitHub/Google pattern), documented explicitly in `docs/VERIFICATION.md`. New
  `GET /api/sessions` and `POST /api/sessions/sign-out-others` routes, deliberately session-cookie-only
  (not `resolvePrincipal`, which would also accept a PAT) since "your active web sessions" has no meaning
  for a PAT-authenticated caller. New identity-integration test coverage (three concurrent sessions all
  listed, sign-out-others removes exactly the others and keeps the caller's own session valid). Verified
  against live PostgreSQL directly, and end-to-end via `curl` simulating two browser tabs: listed both
  sessions with the right one marked `isCurrent`, signed out the other from tab A, confirmed tab A stayed
  authenticated while tab B got a 401, and confirmed a follow-up list showed only the surviving session. A
  quick regression check (comment-editing and board-WIP-limits browser tests, not the full suite, since
  this batch touched no `web/` code) confirmed ordinary cookie login/CSRF-protected writes still work
  after the prior batch's `resolvePrincipal`/`csrfTokenValid` changes. No web UI yet for viewing/signing
  out sessions. Full detail in `docs/VERIFICATION.md`.
- **Phase 6 continued (fixed rate limits, D124/D125), this batch:** new `TicketHub::Web::RateLimiter`
  (`src/web/RateLimiter.h/.cpp`, compiled only into the `ticket-hub` server target plus its own standalone
  test binary -- it has no Crow dependency) implements a thread-safe, in-memory, fixed-window counter per
  key (`std::unordered_map` behind a mutex, with lazy periodic sweeping of expired buckets so the map does
  not grow unbounded). No admin configuration, no per-endpoint/service-account exceptions, matching D124's
  "simple fixed rate limit" answer exactly. Two fixed limiters: `loginRateLimiter()` (20 attempts per IP
  per 15 minutes on `/api/auth/login`, checked before body parsing) and `writeRateLimiter()` (120 requests
  per minute, keyed by `user:<id>` when `resolvePrincipal` resolves a caller else `ip:<remote_ip_address>`)
  -- reused at all 43 of the existing near-identical `csrfTokenValid` call sites (every POST/PUT/PATCH/
  DELETE route) via a single scripted text substitution, plus a one-off overload for the sole route
  (`/api/sessions/sign-out-others`) that resolves a `Domain::Session` (`current`) rather than a
  `Domain::Principal`. Deliberately does not touch the existing per-account login lockout
  (`recordFailedLogin`/`resetFailedLogin`/`isLoginLocked` in both database adapters) -- the new IP-based
  limiter is an additional, complementary layer defending against distributed/enumeration attacks the
  per-account lockout does not cover, not a replacement for it. Deliberately in-memory/process-lifetime
  only (resets on restart) since V1 has no shared cache/job infrastructure
  (`docs/REMOVED_AND_DEFERRED_FEATURES.md`) and this is a single-instance self-hosted install. New
  standalone `ticket-hub-ratelimiter-tests` binary (fixed-window trip/reset, independent per-key buckets)
  -- no Crow or database dependency, so it builds and passes in every configuration including
  SQLite-only and PostgreSQL-only. No database/migration changes, so no live-PostgreSQL check was needed.
  Verified end-to-end via `curl` against a running server: 20 login attempts with a wrong password all
  returned 401, the 21st and 22nd returned 429 with `{"error":"Too many login attempts. Try again
  later."}`; after restarting the server (a fresh in-memory limiter) and logging in, 120 consecutive
  `POST /api/projects` requests with a valid session+CSRF token returned non-429 status codes and the
  121st through 130th all returned 429 with `{"error":"Too many requests. Try again later."}`; a `GET`
  request issued immediately after tripping the write limit still returned 200, confirming only write
  methods are limited. No web UI change (there is nothing to display -- a 429 surfaces to the existing
  fetch-error handling like any other API error). A follow-up in the same batch added a `Retry-After`
  header (900s login / 60s write) to both 429 responses, matching D124's original "429 + Retry-After"
  description (only the admin-configurable multi-level limits were simplified away, not that response
  contract). Full detail in `docs/VERIFICATION.md`.
- **Phase 6 continued (versioned `/api/v1` prefix, D127), this batch:** every route in `Api.cpp` now
  lives under `/api/v1` (a single scripted regex substitution over all 70 `CROW_ROUTE` registrations),
  except `GET /api/health`, deliberately kept unversioned -- the common infra/monitoring-convention
  choice, not specified by any decision text, documented here explicitly. `web/app.js`'s ~63 API call
  sites were updated the same way (every route has to be hand-specified per call; there is no single base-
  URL constant to change in one place). No server/domain/database logic changed -- purely a URL rename.
  Verified via `curl`: the old unversioned `/api/projects` now 404s, `/api/v1/auth/login` and
  `/api/v1/projects` work as before. Full regression pass with the existing Playwright suite (login/
  logout, full project lifecycle including recycle bin and role-gating, issue watch/vote/clone/links/
  edit, reorder/move/bulk actions) all green against the renamed routes, confirming the UI has no
  remaining hardcoded old-prefix paths. Formal `/api/v2` deprecation policy remains deferred until a real
  v2 is needed, per D127. Full detail in `docs/VERIFICATION.md`.
- **Phase 6 continued (read-only CSV export of issues, D48), this batch:** new
  `GET /api/v1/issues/export.csv`, sharing `Domain::IssueFilter`'s query-parameter parsing (factored into
  a new `issueFilterFromQuery(request)` helper) and authorization with the existing
  `GET /api/v1/issues` JSON list route -- an export is always scoped to whatever the caller could already
  see via the list view. New `csvField`/`issuesToCsv` helpers in `Api.cpp` (RFC 4180-style escaping,
  `\r\n` line endings); columns: key/project/summary/description/type/status/priority/reporter/assignee/
  storyPoints/dueDate/resolution/labels(semicolon-joined)/createdAt/updatedAt. No CSV import, no Jira
  migration tool, per D48's explicit scope. `web/`'s Issues view gained an "Export CSV" link (hidden in
  the recycle-bin view) whose `href` is rebuilt from the same filter state as the JSON fetch on every
  re-render, so it always matches the currently visible/filtered issues. No database/migration changes,
  so no live-PostgreSQL check was needed; no new ctest binary either, since this is pure Api.cpp/HTTP-
  layer code verified instead via `curl` and a real Playwright browser download (captured with
  `page.waitForEvent('download')`, confirming the actual downloaded file's name/row count/content, then
  re-verified after filtering by project). Full detail in `docs/VERIFICATION.md`.
- **Phase 6 continued (fixed request/batch-size constants, D125), this batch:** new
  `MaxJsonRequestBodyBytes` (1 MiB) constant enforced at all 20 JSON-body-parsing call sites in `Api.cpp`
  via a single scripted substitution (`request.body.size() > MaxJsonRequestBodyBytes` → `413` before
  parsing), the same technique used for the CSRF/rate-limit chokepoints in earlier batches. New
  `MaxBulkItems` (200) constant enforced in the one shared `requiredIssueKeys(body)` helper all four
  `POST /api/v1/issues/bulk/*` routes already called (`400` if exceeded) -- no per-route changes needed.
  Max page size intentionally not implemented -- it has no meaning until numbered/offset pagination
  (D126) exists; documented as still open rather than faked. No admin configuration for either limit, per
  D125. No database changes, so no live-PostgreSQL check was needed. Verified via `curl` (a ~1.05MB body
  returns 413; 201 bulk `issueKeys` returns 400 with the boundary exactly at 200; ordinary-sized requests
  are unaffected) and a Playwright regression pass of reorder/move/bulk actions (the write paths most
  directly touched, since bulk actions now flow through the new limit). Full detail in
  `docs/VERIFICATION.md`.
- **Phase 6 continued (security hardening pass), this batch, closing out the last non-pagination Phase 6
  item:** while reviewing response headers, found a real vulnerability: an attachment's `contentType` is
  caller-supplied and unvalidated (D98 has no upload-time MIME allow-list), and the download route always
  served `Content-Disposition: inline`, and the app's text/PDF preview rendered inside an unsandboxed
  `<iframe>` -- so a file uploaded with a spoofed `Content-Type: text/html` and a `<script>` payload could
  execute same-origin, either via direct download-URL navigation or via the app's own preview UI. A real
  stored-XSS/CSRF-bypass chain (the readable `th_csrf` cookie would let injected script forge write
  requests), reachable by any project member against any other user including a global admin. Fixed in
  two independent layers: `web/app.js`'s PDF/text `<iframe>` previews now carry `sandbox=""` (the
  load-bearing fix); `Api.cpp`'s download route now serves `Content-Disposition: attachment` for any
  content type matching a new `contentTypeSafeToRenderInline` deny-list (html/xhtml/svg/xml/javascript
  variants), leaving `<img>`/`<audio>`/`<video>`/PDF/plain-text previews unaffected (those elements don't
  honor `Content-Disposition` anyway). Also added standard security headers (`X-Content-Type-Options:
  nosniff`, `X-Frame-Options: DENY`, `Referrer-Policy: same-origin` on every response; a
  `Content-Security-Policy` with `script-src 'self'` on the HTML document only). Dependency review: Crow
  is pinned to release tag `v1.3.3` already (no change). Session/CSRF review: confirmed no regressions to
  cookie flags or CSRF enforcement across this session's earlier batches. Note: the roadmap's referenced
  `handoff/KNOWN_CONSTRAINTS_AND_RISKS.md` does not exist in this repo; reviewed via direct code audit
  instead. Verified via `curl` (headers present correctly; spoofed-`text/html` upload now downloads
  instead of rendering; real PNG still previews inline) and Playwright (all four preview kinds still work
  through the sandboxed iframes; a targeted XSS-reproduction test confirms the payload's `alert()` no
  longer fires -- a regression guard against this fix being reverted). Full detail in
  `docs/VERIFICATION.md`.
- **Phase 6 continued (numbered/offset pagination, D126), this batch, closing out Phase 6:** implementing
  this surfaced a real, previously-undocumented bug: `listIssues` had always had a hardcoded `LIMIT 200`
  in its SQL with no `total` count returned anywhere, silently truncating any result set past 200 rows
  with no way for a caller to detect it. New `Domain::Page<T>` template and fixed
  `DefaultPageSize`/`MaxPageSize` constants (both 200, matching the pre-existing cap -- a caller sending
  neither `page` nor `pageSize` gets exactly the same result set as before). New
  `IDatabase::listIssues(filter, limit, offset)`/`countIssues(filter)` in both adapters, alongside (not
  replacing) the existing unpaginated overload, still used internally and by CSV export, which wants
  everything matching the filter, not one page. New `TicketService::listIssuesPaged` clamps `page`/
  `pageSize` into range. `GET /api/v1/issues`'s response gained `page`/`pageSize`/`totalItems`/
  `totalPages` fields alongside the existing `items` array. New SQLite-integration coverage (limit/
  offset behavior, non-overlapping pages, past-the-end/exceeds-total edge cases, filter+pagination
  composition). Verified against **live PostgreSQL for the first time this session** (found a local
  PostgreSQL 16 cluster already present in this environment, started it, created a throwaway database,
  confirmed identical behavior to SQLite, dropped it afterward) and via `curl` (default params reproduce
  the old behavior exactly; explicit pagination/filtering compose correctly; an out-of-range `pageSize` is
  clamped, not rejected; a non-numeric `page` is `400`; CSV export/dashboard unaffected). Re-ran all three
  build configurations and a Playwright regression pass of the reorder/move/bulk-actions flow (the demo
  UI's heaviest consumer of this route), all green -- confirming the additive response shape doesn't
  break the existing UI. This is a deliberate partial rollout of D126: only `GET /api/v1/issues` is
  paginated; every other list endpoint remains open, documented explicitly. **This closes Phase 6.** Full
  detail in `docs/VERIFICATION.md`.
- **Phase 7 started (backup and restore, D106-D108), this batch:** new `IDatabase::backup(directory)`/
  `restore(directory)` in both adapters. SQLite uses the SQLite online backup API (correct regardless of
  WAL/checkpoint state, unlike a raw file copy of a WAL-mode database); PostgreSQL shells out to
  `pg_dump --clean --if-exists` (self-contained for a direct restore into a non-empty target) and
  `psql -v ON_ERROR_STOP=1` (aborts on the first SQL error rather than silently reporting success on a
  partial failure). New `ticket-hub-cli backup <output-directory>` (copies the attachments directory,
  refuses a non-empty output directory) and `ticket-hub-cli restore <backup-directory> --yes` (mandatory
  confirmation flag, D108; restores DB + attachments, then runs pending migrations as a visible separate
  step, D109). Confirmed D111 (upgrades) needs no new work -- `ticket-hub-cli migrate` already fully
  satisfies it. New SQLite-integration coverage (backup writes a non-empty file; restore reverts a
  post-backup mutation exactly). Verified end-to-end matching Phase 7's exit gate precisely -- "a fresh
  install → seed → backup → restore cycle is scripted and tested on both databases" -- on SQLite and a
  real live local PostgreSQL 16 server (a throwaway database, dropped afterward): seeded, added a marker
  attachment file, backed up, destroyed the live database and attachments directory entirely, confirmed
  restore without `--yes` refuses, then restored with `--yes` and confirmed the issue count, project keys,
  and the marker attachment file all round-tripped correctly on both backends. Re-ran the full three-
  configuration build matrix, all green. Full detail in `docs/VERIFICATION.md`.
- **Phase 7 closed (structured JSON logs and admin version banner, D112/D133), this batch:** new
  `TicketHub::Web::JsonLogHandler` implements Crow's `ILogHandler` and is registered globally via
  `crow::logger::setHandler`, so every log call Crow already makes internally (startup, per-request
  Info lines, warnings/errors) becomes one JSON object per line (`timestamp`/`level`/`service`/`message`)
  on stdout instead of Crow's default plain-text-to-stderr format -- zero new call sites needed elsewhere.
  For the admin banner (D112), confirmed there is no outbound-HTTP-client infrastructure anywhere in this
  codebase and no decision text specifies how the app would discover "the latest version" -- the
  conservative, documented choice: a new global-administrator-only `installation_settings` key
  (`latest_known_version`, no new migration, reusing the existing generic key/value table exactly like
  the anonymous-read-access toggle) that an admin sets manually, compared against the compiled-in
  `TICKETHUB_VERSION`. New `GET`/`PUT /api/v1/settings/latest-known-version` routes (structurally
  identical to the existing anonymous-read-access toggle routes) and a `web/` banner shown only to admins
  when an update is available. **This closes Phase 7's entire roadmap list**, and with it, Milestone 3.
  No database/live-PostgreSQL check needed (reuses already-tested generic setting methods; the logging
  change has no database dimension). Verified via `curl` (JSON log lines all parse correctly; the
  settings GET/PUT round-trip, reject an empty version, and 403 for a non-admin) and Playwright (an admin
  with a configured newer version sees the exact expected banner text; a non-admin sees nothing), plus a
  project-management regression pass and the full three-configuration build matrix, all green. Full
  detail in `docs/VERIFICATION.md`.
- **Phase 8 started (Docker image and Compose distribution, D50), this batch:** new two-stage `Dockerfile`
  (build with the full toolchain, run on a slim `debian:bookworm-slim` image with only the required
  shared libraries plus a non-root user and a real `/api/health`-hitting `HEALTHCHECK`); new
  `.dockerignore`; `docker-compose.yml` gained a `ticket-hub` app service alongside the existing
  `postgres` one, so `docker compose up` alone brings up the full instance -- the prior
  `docker compose up -d postgres`-only workflow is untouched. Explicitly disclosed verification boundary:
  this sandboxed environment's network policy blocks the specific CDN host Docker Hub redirects
  image-layer pulls to (confirmed via the agent proxy's own status log as a policy decision, correctly
  not retried or routed around per this environment's rules), so the actual `docker build` could not run
  here. Verified everything that could be checked without it: `docker build --check`/`docker compose
  config` both pass cleanly, and the exact runtime configuration the container sets was verified directly
  on the host (`cmake --install`'s output tree matches what the Dockerfile's `ENV` lines assume; the
  installed binary runs correctly with that exact configuration against both a fresh SQLite file and a
  live PostgreSQL database using the Compose file's exact connection-string shape; the documented
  `ticket-hub-cli create-user ... --admin` workflow works against the installed binaries). Full detail,
  including the precise proxy-log evidence, in `docs/VERIFICATION.md`.
- **Phase 8 continued (light and dark theme, D46), this batch:** `web/styles.css` gained a full
  `@media (prefers-color-scheme: dark)` variable-override block plus `color-scheme: light dark`, so the
  UI automatically follows the OS-level dark-mode signal with no manual toggle or persisted preference (a
  deliberate scope choice -- D46 only calls for "light and dark theme... simple"). ~30 hardcoded literal
  colors converted to CSS custom properties so every surface themes consistently; new `--surface-hover`/
  `--text-secondary`/`--info-*`/`--success-*`/`--danger-*`/`--warning-*`/`--overlay`/`--topbar-bg`
  variables added. Found and fixed a real bug during verification: `.link-form input` had no explicit
  dark-mode styling and rendered as a stray unreadable white box. Browser-verified with Playwright/
  Chromium: light mode confirmed byte-identical to before, dark mode confirmed via computed styles
  (`body`/`.content`/`.panel`/drawer backgrounds all match the new variables) plus a five-view screenshot
  review (Issues list, issue drawer, create-issue modal, Kanban board, Projects grid), and both existing
  browser regression scripts (`login_browser_test.mjs`, `reorder_move_bulk_test.mjs`) re-run clean with no
  functional regression from this CSS-only change. `ctest --output-on-failure`: 8/8 green (no C++ source
  touched). Full detail in `docs/VERIFICATION.md`.
- **Phase 8 continued (accessibility baseline pass and browser-support note, D47/D139), this batch:**
  reviewed the existing UI against D47's reduced V1 bar ("reasonable baseline accessibility -- semantic
  HTML, keyboard operability -- without a formal WCAG audit deliverable"). Semantic HTML was already
  largely in place; found one real gap: issue table rows, Kanban board cards, project cards, and inline
  issue-key cross-reference links were only click-bound (`element.addEventListener('click', ...)` on a
  plain `<tr>`/`<article>`/`<span>`), with no way for a keyboard-only user to reach or activate them.
  Fixed with a new shared `makeKeyboardActivatable(element, activate)` helper (`tabIndex = 0`,
  `role="link"`, `Enter`/`Space` keydown handler guarded against bubbling from already-independently-
  operable nested controls), wired into the existing `bindIssueLinks()` and the project-card binding; a
  small CSS rule adds a visible focus ring. D139 (browser support) reconfirmed with no code change --
  `web/app.js`'s vanilla, unpolyfilled/untranspiled syntax satisfies "latest two major Chrome/Firefox/
  Edge/Safari" by construction. Browser-verified with Playwright/Chromium: keyboard-only `Tab`+`Enter`/
  `Space` activation confirmed for a table row, a board card, and a project card; confirmed the pre-
  existing stopPropagation guard on the bulk-select checkbox still holds (no mouse-click regression); both
  existing regression scripts re-run clean. `ctest --output-on-failure`: 8/8 green (only `web/` touched).
  Not attempted: a formal WCAG audit, automated contrast-ratio tooling, or screen-reader-software testing
  -- explicitly out of D47's reduced V1 scope. Full detail in `docs/VERIFICATION.md`.
- **Phase 8 complete (threat-model / security self-review), this batch:** a dedicated read of the entire
  HTTP attack surface (every route in `Api.cpp`, every `TicketService`/`AuthService` authorization check,
  both database adapters' query construction, attachment storage, and `web/app.js`'s escaping/CSRF
  handling) found and fixed five issues, written up in full in the new `docs/THREAT_MODEL.md`. The real
  one: `editComment`/`deleteComment`/`editWorklog`/`deleteWorklog`/`deleteAttachment` in
  `TicketService.cpp` checked the project-role requirement against the issue named in the URL but looked
  up the target resource purely by its own id, never confirming it actually belonged to that issue --
  since every project is readable by every authenticated user (D58), this let a user with a role on *any*
  one project reach and mutate a comment/worklog/attachment belonging to a *different* project they have
  no role on, by routing the request through one of their own issues' URLs. Fixed by comparing the
  resource's `issueId` against the URL-resolved issue's `id`, treating a mismatch as "not found" (matches
  the existing unknown-id convention). Four lower-severity fixes alongside it: the CSRF cookie was an
  unnecessary literal prefix of the session token (now generated independently via
  `Common::randomTokenHex(16)`); a login response-time gap let an unauthenticated caller distinguish
  "unknown email" from "wrong password" (now equalized with a lazily-computed dummy-password Argon2id
  verify on the not-found path); `/api/v1/auth/logout` was the one mutating route out of 46 missing a CSRF
  check (added, defense in depth -- `SameSite=Strict` already prevented exploitation); and the CSV export
  was vulnerable to spreadsheet formula injection (fixed with the standard leading-apostrophe mitigation).
  New regression tests in `tests/authorization_integration_tests.cpp` cover the IDOR fix across all three
  resource types. Every fix was reproduced live over HTTP before the change and confirmed fixed after.
  `docs/THREAT_MODEL.md` also records what was reviewed and already correct (password hashing, secret
  storage, SQL parameterization, consistent XSS escaping, path-traversal-proof attachment storage, shell-
  escaped backup/restore commands, and the anonymous-read toggle never weakening write authorization) and
  three deliberate residual risks already implied by earlier fixed-scope decisions (unscoped PATs per
  D39/D40, reverse-proxy-unaware per-IP rate limiting per D124/D125, no socket-level request-body cap).
  `ctest --output-on-failure`: 8/8 green; the default, SQLite-only, and PostgreSQL-only build
  configurations all reconfirmed to compile clean. Full detail in `docs/VERIFICATION.md`.

## `web/` UI now covers every Phase 1-3 route; Phases 4 and 5 are both complete

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
Phase 4 (Collaboration) is now fully implemented and fully covered in the UI: comment editing/tombstone
delete (D81/D82/D83), fixed emoji reactions (D84), @mention handles/the fixed in-app notification set
(D56/D80/D14), the Markdown editor toolbar/live preview (D16), simplified worklogs (D12/D13), and the
admin/security audit log (D23). Phase 5 (Attachments and Kanban board) is now **complete**: the ad-hoc
filter/search widening slice (D10/D43), the personal dashboard widgets (D24), Kanban board WIP limits
(D32/D33), and the full attachments vertical (D15/D98-D105 -- upload, local filesystem storage, four
native-element previews, sortable list/recycle bin/90-day retention, and full Markdown-editor upload/
drag-drop/paste integration) are all done and fully covered in the UI. This closes out Milestone 2. What's
left:

1. **Milestones 1-4, i.e. the entire reduced-scope V1 roadmap, are now fully complete** per
   `docs/REDUCED_SCOPE_ROADMAP.md` -- see "The roadmap is now complete" below.
2. Optional UX polish that was never part of the write-route coverage goal is now **fully done**:
   drag-and-drop card movement on the Board view (post-V1 batch 3), the bulk Done-status picker with a
   shared-resolution prompt, and keyboard-driven multi-select for the issues table (both post-V1 batch 4)
   -- see "The roadmap is now complete" below.
3. Re-typing (`issueTypeKey`) and re-parenting (`parentIssueKey`) an issue after creation, previously the
   one item left deliberately unimplemented, is now done (post-V1 batch 2 -- see "The roadmap is now
   complete" below), including a Type/Parent picker in the issue drawer's edit form.

## The roadmap is now complete

Phase 3's core/CLI/test/server/UI layer is now **fully complete**: re-typing (`issueTypeKey`) and
re-parenting (`parentIssueKey`) an issue after creation -- the one item left open since Phase 3 -- was
added as post-V1 batch 2 (below). `moveIssue` (D37) still deliberately does not re-parent/un-parent as
*part of a project move* -- an issue with a parent or children must be edited via `editIssue` first to
clear them, then moved -- that split is a deliberate design choice (moving projects and changing hierarchy
position are different operations), not a remaining gap.

Milestone 2 (Phases 4 and 5) is **fully closed**. Milestone 3 (Phase 6: REST API v1/export; Phase 7:
backup/restore/upgrade/observability) is **fully closed**: personal access tokens (D39/D40), the
active-session list/"sign out everywhere" endpoint (D54), fixed rate limiting (D124/D125), the versioned
`/api/v1` prefix (D127), read-only CSV export (D48), fixed request-body/bulk-item constants (D125), the
Phase 6 security hardening pass, pagination for `GET /api/v1/issues` (D126, a deliberate partial rollout --
every other list endpoint remains unpaginated and documented as such), backup/restore (D106-D108,
live-verified on both databases), the upgrade mechanism (D111, already satisfied by `ticket-hub-cli
migrate`), structured JSON logs to stdout (D133), and the in-app admin version banner (D112). **Milestone 4
(Phase 8: packaging and release hardening) is now fully closed too**: the Docker image and Compose
distribution path (D50), light/dark theme (D46), the accessibility baseline pass (D47), the browser-support
note (D139), and the threat-model/security self-review (`docs/THREAT_MODEL.md`) are all done.

**That closes every phase in `docs/REDUCED_SCOPE_ROADMAP.md`.** The roadmap's own Phase 8 exit gate --
"`docker compose up` produces a usable, documented V1 instance; all supported build configurations...
compile and pass tests; no known open security issue from the hardening pass" -- is met: Docker/Compose
verified as far as this environment's network policy allows (see `docs/VERIFICATION.md`); the default,
SQLite-only, and PostgreSQL-only configurations all compile and their test suites pass; and every finding
from the security self-review is either fixed or recorded as an explicit, decision-consistent accepted
residual risk in `docs/THREAT_MODEL.md`, not a silent gap.

**Every item identified as optional, non-roadmap follow-up when the reduced-scope V1 roadmap closed is now
done** (post-V1 batches 1-4, below). One further item was added since at explicit user request -- Jira-
style `/browse/{key}` direct issue links (post-V1 batch 5, below). Post-V1 batch 16 (below) was a bug fix,
not new scope. There is currently no further work queued -- anything beyond this point is new scope and,
per the same rule that has applied to `docs/REMOVED_AND_DEFERRED_FEATURES.md` all along, should not be
started without a fresh, explicit product conversation.

**Post-V1 batch 1 (done):** the user was asked to pick the first piece of optional follow-up and chose a
web UI for managing personal access tokens and active sessions -- both already had a complete REST API and
CLI-equivalent story since Phase 6 (D39/D40/D54); only the `web/` surface was missing. New "Account" nav
item/view in `web/index.html`/`app.js`, visible to every authenticated user: create/list/revoke personal
access tokens (the raw token shown exactly once, per D40, then never re-shown), list active sessions with
the current one badged, and "sign out everywhere else". No backend or schema changes -- purely a new
consumer of existing, already-tested endpoints. Browser-verified end-to-end with Playwright/Chromium,
including a genuine two-cookie-jar session test confirming "sign out everywhere else" actually invalidates
the other session server-side (not just hides it in the UI) while preserving the caller's own. Full detail
in `docs/VERIFICATION.md`.

**Post-V1 batch 2 (done):** the user was asked to pick the next piece of optional follow-up and chose
re-typing/re-parenting an issue after creation -- the one deliberate gap left open since Phase 3.
`Domain::EditIssueRequest` gained `issueTypeKey`/`parentIssueKey`; `TicketService::requireValidHierarchy`
was refactored into a shared `validateHierarchyShape` used by both `createIssue` and `editIssue` (same
Epic/Sub-task/same-project/parent-level rules as creation, plus a new self-parent guard). Whether a
hierarchy-level retype would orphan existing children depends on concurrent database state, so that one
check runs transactionally inside `IDatabase::editIssue` in both adapters -- same "has children" precedent
`moveIssue` already established -- rejecting a level-crossing retype (Epic <-> Story/Task/Bug <-> Sub-task)
only when the issue currently has children; same-level retyping (e.g. Task -> Bug) is always allowed.
New `issue_type`/`parent` `issue_history` rows on change. New Type/Parent controls in the issue drawer's
edit form, mirroring the create modal's picker (`refreshEditParentOptions`). New hierarchy-edit test
coverage in `tests/workflow_integration_tests.cpp`; three existing `EditIssueRequest` construction sites
across two other test files needed an explicit `issueTypeKey` once the field became required (deliberately
*not* defaulted the way `CreateIssueRequest`'s is, since silently defaulting an edit's missing type could
silently retype an issue). Verified end-to-end over the real HTTP API against **live PostgreSQL** (this
touches both database adapters) -- created an Epic, retyped it with no children (succeeded), created an
Epic with a child and confirmed the cross-level retype was rejected, then re-parented the child to a third
Epic and confirmed both new history rows via `psql`. Browser-verified with Playwright/Chromium including
the server-rejection path (error banner shown, edit form stays open, no silent data loss). Full detail in
`docs/VERIFICATION.md`.

**Post-V1 batch 3 (done):** the user was asked to pick the next piece of optional follow-up and chose
drag-and-drop card movement on the Kanban board -- previously the board could only change an issue's
status via the drawer's dropdown. New `bindBoardDragAndDrop()`/`handleBoardDrop()`/
`applyBoardStatusChange()` in `web/app.js`: cards are `draggable`, columns are drop targets keyed by
`data-status-key`; dropping on a different column applies the status change directly (via the existing
`PATCH /api/v1/issues/{key}/status` route, unchanged); dropping on the same column is a no-op; dropping on
a Done-category column without an existing resolution opens a small dynamically-built dialog (reusing the
existing `.modal-backdrop`/`.modal` styling) prompting for one first, mirroring the same D68-D70 rule the
drawer's status dropdown already enforces. No backend, schema, or API changes. Verified with Playwright/
Chromium; discovered along the way that Playwright's built-in `dragTo()` helper is unreliable for longer-
distance HTML5 drag simulation specifically in headless Chromium (short adjacent-column drags worked,
longer ones consistently failed to fire `dragover` on the target regardless of viewport size) -- switched
to a manual multi-step mouse simulation for the verification script, which reliably reproduced every
scenario: direct move, same-column no-op, Done-column resolution prompt (shown, cancelable without side
effects, confirmable). Screenshotted in both light and dark mode. Both existing browser regression scripts
re-run clean. Native HTML5 drag-and-drop has no keyboard equivalent; the drawer's status dropdown remains
the keyboard-operable path (verified as part of the earlier D47 accessibility pass), so this adds a faster
mouse-only option rather than replacing the accessible one. Full detail in `docs/VERIFICATION.md`.

**Post-V1 batch 4 (done):** the user was asked to pick the next piece of optional follow-up and chose both
remaining items together -- the bulk Done-status picker and keyboard-driven multi-select. Bulk status
changes to a Done-category status previously weren't reachable from the bulk bar at all (the picker
excluded them client-side, even though the server already accepted and forwarded a shared `resolution` to
every issue in the batch); fixed by including them in `#bulk-status-select` again and adding a
`#bulk-resolution-select` that appears exactly when a Done-category status is chosen, mirroring the
drawer's existing reveal-on-selection pattern. Keyboard multi-select added a "select all" checkbox in the
issues table header (checked/unchecked/indeterminate tri-state synced to the current selection),
Shift+click range selection between the last-clicked checkbox and the current one, and Shift+ArrowDown/
ArrowUp on a focused checkbox to extend a range one row at a time and move focus along with it -- a
genuinely keyboard-only path the shift-click convention alone doesn't provide. Both changes are entirely
`web/app.js` -- no backend, schema, or API changes (the bulk-status backend already supported the shared
resolution; basic single-checkbox keyboard toggling already worked via native `<input type="checkbox">`
semantics). Browser-verified with Playwright/Chromium: confirmed the resolution picker's reveal/hide
behavior and that a bulk Done transition applies the same resolution to every selected issue (checked via
a direct API read afterward); confirmed the header checkbox toggles all/none; confirmed Shift+click checks
every row in a range, not just the endpoints; confirmed two consecutive Shift+ArrowDown presses from a
freshly-checked first row grow the selection by one each time with focus moving along; reconfirmed the
pre-existing "checkbox click never opens the drawer" guard still holds. Screenshotted in both light and
dark mode. Both existing browser regression scripts re-run clean. This closes out every item on the
optional-follow-up list identified when the reduced-scope V1 roadmap closed. Full detail in
`docs/VERIFICATION.md`.

**Post-V1 batch 5 (done):** the user asked whether Ticket Hub supports Jira-style `/browse/ABC-123` direct
issue links -- it didn't (a grep for `history.`/`pushState`/`location.` in `web/app.js` found nothing; the
app was pure client-state with zero URL routing), so this batch added one. New `GET /browse/<string>`
route in `src/web/HttpServer.cpp`, serving the identical `index.html` shell (and identical security
headers) as `/`. `web/app.js` keeps the URL synced via `history.pushState` as issues open/close, guarded so
a new history entry is only pushed when the URL doesn't already match the target issue -- the same guard
makes it safe for every existing call site that re-opens the same issue after a mutation (edit, comment,
watch/vote, worklog, clone, move, ...) without spamming the browser's back-button history with duplicate
entries, and means the new `popstate` listener (for Back/Forward support) doesn't need a separate
"don't re-push" flag either, since the browser has already updated the URL by the time it fires. The URL
is also checked once after initial page load and once after a successful login, so a deep link works
whether a session already existed or not (no extra code was needed for "session expired while sitting on
`/browse/TH-5`, then logs back in" -- the URL just never changes while the login screen is up). No
backend/schema changes beyond the one new static route. Browser-verified with Playwright/Chromium across
every combination: logged-out direct navigation, clicking between issues, a full page reload while on a
`/browse/{key}` URL, Back/Forward, an unknown key (shows the existing error banner, no crash), and
confirming `history.length` doesn't grow across repeated mutations on an already-open issue. Both existing
browser regression scripts re-run clean. Full detail in `docs/VERIFICATION.md`.

**Post-V1 batch 6 (done):** the user asked to rename "issue" to "ticket" everywhere, in both the UI and
the database tables; asked to clarify the scope (REST API paths and internal C++ naming too, or UI/DB
only), the user chose the fully comprehensive option. New migration `016_ticket_terminology.sql` (both
backends) renames every `issue*` table/column/index; the PostgreSQL variant also renames the
auto-generated constraint names a table/column `RENAME` doesn't touch on its own, for full consistency.
Every C++ type, method, and identifier (`Domain::Issue` -> `Domain::Ticket`,
`TicketService::createIssue` -> `TicketService::createTicket`, etc.), every REST route and JSON field
(`/api/v1/issues` -> `/api/v1/tickets`, `issueKey` -> `ticketKey`, ...), every CSS class, and all
user-facing UI text were renamed via a case-preserving substring pass (`Issue`->`Ticket`, `issue`->
`ticket`), applied only to the fixed list of files known to reference the domain entity (not blindly
repo-wide), followed by a manual grep-based fix-up pass for the one grammar artifact this kind of
substitution predictably produces ("an issue" -> "an ticket", fixed to "a ticket" everywhere it appeared,
across C++ comments/error strings, test descriptions, docs, and `web/app.js`'s user-visible text).
`002_seed_demo.sql` (both backends) updated to match the renamed columns. Documentation split by nature,
consistent with how this project has always treated its own written history: current-state documents
(`docs/SCHEMA.md`, `docs/SCOPE.md`, and the non-narrative sections of `README.md` -- feature list,
architecture, API reference) were renamed in place; the dated historical batch narratives in `README.md`'s
"Server verification" section, this file, `CHANGELOG.md`, and `docs/VERIFICATION.md` were left exactly as
written, since they describe what was literally true, and named, at the time -- a new dated entry was
added to each instead. `PLAN.md`'s per-batch history was likewise left as written, with a new bullet added.
Verified: full rebuild across the project's build configuration with zero new warnings/errors, `ctest`
clean; a fresh `migrate` + `seed-demo` against a throwaway PostgreSQL database, followed by a direct
`pg_constraint`/`pg_indexes` query confirming zero remaining "issue"-named tables, indexes, or constraints
anywhere in the schema; the same fresh-migrate-and-seed check against a throwaway SQLite database via
`sqlite_master`; a live HTTP smoke test against the PostgreSQL-backed server (login, `GET
/api/v1/tickets`, `/browse/{key}`); and a full Playwright/Chromium browser pass confirming no leftover
"Issue"/"Issues" text anywhere in the rendered UI (dashboard, nav, tickets table, ticket drawer) and that
creating a new ticket through the UI still works end-to-end. Full detail in `docs/VERIFICATION.md`.

**Post-V1 batch 7 (done):** the user asked what entity fields tickets support (priority, components,
labels, created/updated) -- priority/labels/created/updated all check out, but components turned out to be
a real gap: D19 in `docs/REDUCED_SCOPE_DECISIONS.md` decided to `KEEP_FOR_V1` "simple project components:
name, description, lead, default assignee; at most one per issue," but no `project_components` table, no
`component_id` column, and no component-related code existed anywhere. Asked to implement it, and did. New
migration `017_project_components.sql` (both backends): `project_components` plus `tickets.component_id`
(nullable, `ON DELETE SET NULL`) -- no recycle bin/soft-delete, since D19 doesn't call for one, unlike
tickets/projects/comments. New `Domain::ComponentSummary`/`ProjectComponent`/`CreateComponentRequest`/
`EditComponentRequest`, `IDatabase` component CRUD in both adapters, and `TicketService::listComponents`/
`createComponent`/`editComponent`/`deleteComponent` (project-Admin-or-above to write, same read access as
projects/tickets otherwise; edit/delete scoped to `(projectKey, componentId)` together, the same IDOR-safe
pattern the Phase 8 threat-model pass established for worklogs/comments/attachments).
`createTicket`/`editTicket` gained a `componentName` field (resolved by name against the ticket's own
project, like `assigneeEmail`/`priorityKey`); `TicketFilter` gained a matching filter; `cloneTicket` now
copies the component too, per D60's own original clone-field list (which already named "component" -- it
was simply unreachable before this batch). New REST routes: `GET`/`POST
/api/v1/projects/{key}/components`, `PATCH`/`DELETE /api/v1/projects/{key}/components/{id}`; `GET
/api/v1/tickets` gained a `component` query parameter. `web/` gained a "Components" management dialog on
each project card, a Component picker in the create-ticket modal and the ticket drawer's edit form, a
read-only Component row in the drawer, and a Component filter in the tickets table's filter bar. Verified:
full rebuild in all three build configurations, `ctest` clean including three new test files' worth of
coverage (domain validation, SQLite integration -- including the ON DELETE SET NULL behavior and the
componentName filter -- and an authorization IDOR regression); a fresh live-PostgreSQL migrate+seed with a
full HTTP-layer smoke test (create/edit/delete a component, create a ticket referencing it, filter by it,
delete the component and confirm the ticket's `component` field goes back to `null`); and a
Playwright/Chromium browser pass, which found and fixed one real layout bug (the components list row
misused the `.meta-row` class, meant for a stacked label/value pair, not a label-plus-action-button row --
replaced with an explicit flex row). Full detail in `docs/VERIFICATION.md`.

**Post-V1 batch 8 (done, 2026-08-03):** asked to analyze the gap between the V1 decision register and what
is actually implemented. Audited all 142 decisions in `docs/REDUCED_SCOPE_DECISIONS.md`, filtering to
`KEEP_FOR_V1`/`ALREADY_IMPLEMENTED_AND_KEEP`, and verified each against the real codebase. Found five real
gaps (and one false alarm: D101's attachment sortable list is, in fact, correctly implemented). Asked to
implement all five, and did:

- **D62 (Markdown checklist syntax)**: `renderMarkdown`'s unordered-list branch now detects `- [ ] foo`/
  `- [x] bar` items via a regex and emits a disabled `<input type="checkbox">` (checked to match) instead
  of literal bracket text. Pure client-side rendering change, no backend/schema involvement.
- **D66 ("No Epic" ticket filter)**: a new `#ticket-epic-filter` select on the Tickets view, client-side
  only (matching D10's ad-hoc-filter philosophy) -- filters the already-fetched ticket list to
  Story/Task/Bug tickets (`ticketTypeHierarchyLevel(...) === 0`) with no parent, excluding both Epics
  (which have no "Epic" of their own by definition) and Sub-tasks (whose parent is never an Epic).
- **D129 (stale-write conflict dialog)**: `api()` now attaches `error.status` to a thrown error; a 409 from
  `PATCH /api/v1/tickets/{key}` (status change or full edit) now opens a `showConflictDialog` modal
  ("Someone else changed this ticket... Reload latest version") instead of a generic toast. "Reload"
  re-opens the ticket in edit mode with fresh server data rather than attempting any per-field merge (this
  app has no diff/merge machinery).
- **D45 (per-user timezone/clock-format preferences)**: new `PATCH /api/v1/account/preferences`
  (`IDatabase::updateUserPreferences`, `Domain::validateUpdatePreferences`) writes the `users.time_zone`/
  `clock_format` columns that already existed in the schema but had no self-service path. New "Preferences"
  panel on the Account view, with a "Detect from browser" button. `Domain::Principal` gained `timeZone`/
  `clockFormat` fields (default-initialized so the existing 4-argument brace-init call sites across the
  codebase keep compiling). The "has this user manually set a preference" signal lives in the browser's own
  `localStorage`, not a server column -- deliberately, so auto-detect fires at most once per browser and
  never overwrites a deliberate choice (including a deliberate UTC), and so the same account can correctly
  carry different preferences on different devices/timezones. `formatDate` now distinguishes a date-only
  value (`YYYY-MM-DD` -- ticket due dates, worklog dates) from a full timestamp by regex and renders the
  former in UTC with no time-of-day and no shift, per D45's own decision text ("date-only stays date-only"),
  while the latter renders in the viewer's stored timezone/clock format.
- **D91 (changing an active project's key)**: new `PATCH /api/v1/projects/{key}/key`
  (`IDatabase::changeProjectKey`, project-Admin-or-above), one transaction: the vacated key becomes a
  permanent `project_key_aliases` row (this table existed in the schema for the "reserved while in the
  recycle bin" invariant, D90, but had never been written to for an active rename), and every ticket in the
  project -- including soft-deleted ones -- is renamed to the new prefix with the same numeric suffix, its
  own vacated key becoming a `ticket_key_aliases` row exactly like `moveTicket` (D38) already does for a
  single ticket. Rejects a `newKey` already live or already reserved by another project's alias. New
  "Rename key" button/modal on each project card.

Two real bugs were caught and fixed during this batch's own browser verification, not scoped to D91's
description alone but surfaced by it: (1) the web client's `state.selectedProject` was not updated when the
*currently selected* project was the one renamed, so the Tickets/Board views silently went empty (filtering
by a project key that no longer resolves) until the user manually reselected a project -- fixed by having
the rename handler follow the key when it matches `state.selectedProject`; (2) `.modal-backdrop` shared a
`z-index: 80` with `.drawer-backdrop`, lower than `.ticket-drawer`'s `z-index: 90`, so any modal opened
while the ticket drawer is showing -- most importantly D129's own conflict dialog, which is triggered by a
failed save from inside the drawer's edit form -- rendered behind the drawer and was unclickable; fixed by
giving `.modal-backdrop` its own `z-index: 100`, above both.

New tests: `tests/domain_validation_tests.cpp` (`validateUpdatePreferences`/`isValidClockFormat`, valid and
invalid cases); `tests/sqlite_integration_tests.cpp` (`updateUserPreferences`+`findUserById` round-trip;
`changeProjectKey` -- success, alias row created, every ticket bulk-renamed with old keys aliased,
live-key collision rejected, alias-reserved-key collision rejected, unknown project key returns nullopt);
`tests/authorization_integration_tests.cpp` (`changeProjectKey` requires project-Admin-or-above, not just
global admin; self-rename rejected; live-key and alias-key collisions rejected; unknown key returns nullopt
without throwing). Verified: full rebuild and `ctest` clean in all three configurations (default,
`-DTICKETHUB_WITH_POSTGRES=OFF`, `-DTICKETHUB_WITH_SQLITE=OFF`) with zero new warnings; a fresh live
PostgreSQL database and a fresh live SQLite database each exercised end-to-end over HTTP with `curl`
(preferences round-trip and persistence; project-key rename with both collision cases; the renamed
project's and its tickets' old keys still resolving via `ticket_key_aliases`; the 409 conflict response);
and a full Playwright/Chromium browser pass against a fresh SQLite database covering all five behaviors
(13/13 checks passed after the two fixes above). Full detail in `docs/VERIFICATION.md`.

**Post-V1 batch 9 (done, 2026-08-03):** three user-requested changes: "uprav board backlog muze byt
obrovsky a nebude v vlastnim sloupci backlog ukoly by mely mit svoji specialni obrazovku" ("fix the board
-- backlog can be huge and won't be in its own column, backlog tickets should have their own dedicated
screen") and, mid-batch, "kazdy ticket bude mit komentarea worklogy. worklogy budou take markdown
supported a ne jednoradkove nasilne jako nyni. ticket musi mit i created a updated" ("every ticket will
have comments and worklogs; worklogs will also be Markdown-supported and not forced single-line as now;
a ticket must also have created and updated").

- **Backlog off the board, onto its own screen (amends D32).** D32 ("one board column equals one workflow
  status") originally put Backlog on the board like every other status; removed (board is now 4 columns:
  Confirmed, In Progress, In Review, Done) and replaced with a new dedicated "Backlog" nav screen
  (`renderBacklog` in `web/app.js`), the first list view in this app to use real server-side pagination
  (`page`/`pageSize`, D126's existing contract) rather than the fixed 200-row cap every other list still
  relies on -- justified because backlog size is explicitly unbounded by design, unlike every other list.
  Ordered by a new `sort=rank` query parameter (`Domain::TicketFilter::sortByRank`; both adapters'
  `listTickets` overloads switch `ORDER BY` from `updated_at DESC` to `rank_order, ticket_number` when set,
  a small backward-compatible addition -- omitting the parameter keeps every existing caller's behavior
  unchanged) so "page 1" is always the top of the backlog by priority. The existing manual-reorder arrows
  (D31) work exactly as they already do on the Tickets view, just always enabled here since the screen is
  always scoped to one project. The Board's own ticket fetch also changed: `fetchBoardTickets()` now issues
  one status-filtered request per board column instead of one unfiltered `fetchTickets()` call, so the
  board's fixed page-size budget is spent entirely on the four statuses it renders rather than possibly
  being consumed by backlog rows that would never have appeared on it anyway -- a real correctness gap the
  old single-fetch approach had once a project's backlog grew past ~200 tickets, not just a display
  preference. "Board"/"Backlog" shortcut buttons link the two screens both ways.
- **Worklogs are Markdown-supported, not forced single-line.** The "what did you work on" field was a
  plain `<input>` rendered with `escapeHtml`; the backend already allowed up to 10,000 characters with no
  single-line restriction (`Domain::validateAddWorklog`), so this was purely a frontend gap. Changed to a
  `<textarea>` with the same Markdown toolbar, live preview, and @mention autocomplete already used for
  comments and the description field (`attachMarkdownToolbar`/`attachMentionAutocomplete`), rendered
  through `renderMarkdown` into a `.markdown-body` block.
- **Created/Updated columns on every ticket list.** The ticket detail drawer already showed both; the
  Tickets view and the new Backlog screen's tables did not. `ticketRows` (shared by both) gained the two
  columns behind a new `showTimestamps` option (default `true`); the Dashboard's compact "Assigned to
  me"/"Watching"/"Recently active" widgets (`tablePanel`) explicitly opt out to stay terse, matching their
  own "focused overview" framing -- no schema change, since `createdAt`/`updatedAt` were already returned
  by every ticket JSON response.

New test coverage: `tests/sqlite_integration_tests.cpp` asserts `sortByRank` orders both the unpaginated
and paginated `listTickets` overloads by `rank_order`/`ticket_number`, and that omitting it preserves the
pre-existing `updated_at`-based order. Verified: full rebuild and `ctest` clean in all three build
configurations; a fresh live PostgreSQL database exercised over HTTP (a board-status-filtered fetch
excludes backlog tickets; a `sort=rank` paginated backlog listing returns the lowest-rank ticket first; a
multi-line, Markdown-syntax worklog comment round-trips byte-for-byte); a full Playwright/Chromium browser
pass against a fresh SQLite database seeded with 60+ backlog tickets (19/19 checks: no Backlog column on
the board, both shortcut buttons, correct page-1/page-2 row counts and Previous/Next disabled states,
reorder arrows changing row order across a page, Created/Updated columns present on Tickets/Backlog and
absent on the Dashboard's compact widgets, the worklog field being a Markdown-toolbar textarea, and a
submitted worklog rendering **bold** text and a bullet list as real HTML). Full detail in
`docs/VERIFICATION.md`.

**Post-V1 batch 10 (done, 2026-08-03):** "prosim at je layout detailu ticketu vice podobny jire" ("please
make the ticket detail layout more similar to Jira"). A pure UI/layout change (no API or schema changes),
restructuring the ticket drawer's markup and CSS while leaving every element `id`/`data-*` attribute and
event handler untouched -- only the surrounding structure moved, so no JavaScript logic needed to change
except for the one genuinely new thing, the Activity tabs.

- **Status pill.** The status `<select>` moved out of the sidebar and into a prominent, colored
  pill-button (`.status-pill`, with `.status-pill--todo`/`--in_progress`/`--done` modifier classes reusing
  the exact same category colors `.status-chip` already used elsewhere) sitting directly under the title,
  with the resolution picker/confirm-cancel flow inline right next to it (`.resolution-inline`) instead of
  a separate labeled row buried in the sidebar.
- **Sidebar as two panel cards.** The flat `meta-list` became two bordered "Details" (Assignee, Reporter,
  Priority, Labels, Component, Story points, Due date, Parent, Move to project) and "Dates" (Created,
  Updated) panel cards (`.sidebar-panel`), mirroring Jira's own Details/Dates panel split instead of one
  undifferentiated list.
- **Activity tabs.** Comments and Work log became tabs in one "Activity" section (`.activity-tabs`, one
  `.activity-panel` each) instead of two always-visible stacked sections, matching Jira's own tabbed
  activity area. A new module-level `activeActivityTab` variable (not scoped inside `openTicket`, so it
  survives a full drawer re-fetch) tracks which tab is showing; logging time or posting a comment keeps
  the relevant tab active afterward, so the new entry is visible immediately without an extra click. Each
  panel's add-form now sits above its list (matching Jira's comment-box-at-top convention) instead of
  below it. "Links" was relabeled "Linked issues" to match Jira's own terminology.

Two real bugs were found and fixed during this batch's own browser verification:

1. **Pre-existing backend bug, not introduced by this batch:** `changeTicketStatus` (both adapters) had a
   same-status short-circuit (`if (oldStatus == statusKey) { COMMIT; return true; }`) that unconditionally
   no-opped a same-status call, silently discarding any resolution supplied with it -- including the one
   legitimate case where a ticket is already Done-category but has no resolution recorded yet (reachable
   with historical/imported data, not through the normal API) and the resolution-confirm UI is used to set
   one retroactively without changing the status. This flow existed identically in the old sidebar-buried
   layout, so it's not a bug this batch introduced -- but the redesign makes the resolution-confirm control
   more prominent, so it was fixed here rather than left broken behind a newly-showcased UI element. Fixed
   by narrowing the no-op guard: a same-status call is now applied (not skipped) specifically when the
   ticket is Done-category, has no resolution yet, and a valid one is being supplied; every other
   same-status call remains a pure no-op, matching D68-D70's "any other transition leaves resolution
   alone."
2. **CSS regression introduced by this batch:** `.resolution-inline { display: flex; ... }` has the same
   selector specificity as the browser's default `[hidden] { display: none }` rule and comes later in the
   cascade, so it silently overrode `hidden`, meaning the resolution picker showed even for non-Done
   statuses (caught by comparing an "In Progress" ticket's screenshot against expectations). Fixed with an
   explicit `.resolution-inline[hidden] { display: none; }` override.

New test coverage: `tests/sqlite_integration_tests.cpp` covers the resolution no-op-guard fix directly --
forces a ticket into Done with no resolution via raw SQL (the only way to reach that state, since the
normal API can't produce it), confirms a same-status call with a resolution now persists it and bumps the
optimistic-lock version, and confirms a further same-status call once a resolution already exists remains
a true no-op (neither overwriting the resolution nor bumping the version). Verified: full rebuild and
`ctest` clean in all three build configurations; the resolution-confirm fix live-verified over real HTTP
against both a fresh PostgreSQL database and a fresh SQLite database (same-status confirm now persists,
a second same-status call stays a no-op); a full Playwright/Chromium browser pass (20/20 checks) covering
the pill/toolbar/sidebar-panel structure, both activity tabs (including that logging time keeps the Work
log tab active and the new entry's Markdown renders correctly), every pre-existing action still working
through the restyled markup (watch/edit/clone/status-change/resolution-confirm), and screenshots of all
three status-category pill colors plus dark mode confirming the `[hidden]` CSS fix. README's ticket-detail
screenshot regenerated to show the new layout. Full detail in `docs/VERIFICATION.md`.

**Post-V1 batch 11 (done, 2026-08-03):** "a co pridat i zalozky history activity transitions ???" ("and
what about also adding History/Activity/Transitions tabs?") -- asked right after batch 10's Activity tabs
landed. `ticket_history` (`id`, `ticket_id`, `actor_user_id` nullable, `field_name`, `old_value`,
`new_value`, `created_at`) already existed and was already written to by `changeTicketStatus`, `editTicket`,
and `moveTicket`, but had no read-side API of its own -- purely write-only internal bookkeeping until now.

- **Backend.** New `IDatabase::listTicketHistory(ticketKey)` on both adapters, mirroring the existing
  `listComments`/`listWorklogs` per-ticket-list pattern exactly (same signature shape, same
  `TicketSelect`-style SQL constant, newest-first ordering), the one structural difference being a
  `LEFT JOIN users` (not `JOIN`) for the nullable `actor_user_id`, since a comment/worklog's
  `author_user_id` is `NOT NULL` but a history row's actor is not (and never null-checking it would have
  crashed on the very first row with no resolvable actor). `TicketService::listTicketHistory` is a plain
  read-access-gated passthrough, and a new `GET /api/v1/tickets/{key}/history` route in `src/web/Api.cpp`
  serializes it (`ticketHistoryEntryJson`).
- **Frontend.** The drawer's Activity section gained a third tab, "History (N)", next to Comments and Work
  log. Each row renders as a Jira-style sentence -- "Demo Admin changed priority from "Highest" to
  "Medium" / 2 minutes ago" -- via new formatting helpers (`historyChangeText`, `historyValueLabel`,
  `historyFieldLabel`) and small client-side-only lookup maps (`PRIORITY_LABELS`, `TICKET_TYPE_LABELS`,
  `HISTORY_FIELD_LABELS`) that turn the raw stored keys (a status/priority/type key, a snake_case field
  name like `story_points`) into the same display labels used everywhere else in the UI -- a deliberately
  minimal, frontend-only addition rather than expanding the API response or refactoring the many
  pre-existing inline dropdown option lists elsewhere in `web/app.js`. No schema or migration change --
  this batch is additive read-exposure of an existing table only.

New test coverage: `tests/sqlite_integration_tests.cpp` asserts `listTicketHistory` returns the
status-change row plus one row per field actually changed by a full edit, newest-first ordering, correct
old/new values on the status entry, actor resolution through the nullable FK, and an empty result for an
unknown ticket key. One test-authoring bug was caught and fixed along the way: the ordering assertion
initially used `std::is_sorted` with a `>=` comparator, which is not a valid strict weak ordering (fails
irreflexivity) and spuriously failed whenever two history rows from the same `editTicket` transaction
shared an identical `created_at` -- confirmed via live `curl` against a running server that the actual
query order was correct all along, then fixed the comparator to strict `>`. Verified: full rebuild and
`ctest` clean in all three build configurations; live-verified over real HTTP against both a fresh
PostgreSQL database and a fresh SQLite database (a status transition and a full edit each produce the
expected rows with correctly resolved actor and old/new values; a genuine same-status no-op correctly
produces no new row); a full Playwright/Chromium browser pass (`verify_history_tab.js`, 8/8 checks)
covering the three-tab layout, the History panel hidden by default and visible after its tab is clicked, a
real edit producing readable "changed summary"/"changed priority" entries with humanized labels rather
than raw snake_case field names or raw keys, and a status transition producing a readable status-name
entry. README's ticket-detail screenshot regenerated to show the History tab active. Full detail in
`docs/VERIFICATION.md`.

**Post-V1 batch 12 (done, 2026-08-03):** offered a menu of possible new functionality
("napis mi seznam moznych novych funkcionalit a ja se rozhodnu"), grouped into quick/safe UI additions and
larger decision-register-deferred features; the user picked six by number
("implementuj prosim 1 3 4 6 11 12"). This batch covers the three quick ones (1, 3, 4); the other three
(6 = custom fields, 11 = webhooks, 12 = email) are tracked as ongoing work in `docs/SCOPE.md`'s "Deferred
after V1, in progress" note.

- **Quick filters (Board/Backlog).** Two chip toggles, "Only my tickets" and "No Epic"
  (`state.quickFilterMine`/`quickFilterNoEpic`, deliberately separate from the Tickets screen's own
  filter-bar state), applied client-side after the normal fetch via a shared `applyQuickFilters`/
  `quickFiltersBar` pair so the same code covers both the unpaginated Board and the paginated Backlog.
  Reuses the exact same "assignee equals me" and "no parent, Story/Task/Bug type" semantics the Tickets
  screen's assignee dropdown and D66's "No Epic" filter already established.
- **More keyboard shortcuts.** `/` focuses the global search box; `?` opens a new "Keyboard shortcuts"
  help modal (also reachable via a topbar button); table rows and board cards were already focusable and
  Enter-activatable (`makeKeyboardActivatable`, from the D47 accessibility pass) but only reachable one
  Tab press at a time -- `bindTicketLinks` now wires Up/Down to jump directly to the previous/next ticket
  in reading order, and a new `bindBoardKeyboardNav` wires Left/Right on the board to move to the same row
  position in the adjacent column (a plain "next in DOM order" rule, as Up/Down uses, would just walk down
  the current column instead, since board cards are grouped by column in the markup).
- **Pagination (D126) extended to notifications and the audit log.** Scoped down from the original
  four-endpoint ask: a per-ticket comment/worklog list is naturally bounded, matching the same reasoning
  Batch 11 already applied to `ticket_history`, so only notifications (unbounded per user account) and the
  audit log (unbounded, installation-wide, append-only forever) actually needed it. New
  `IDatabase::listNotifications(userId, unreadOnly, limit, offset)`/`countNotifications` and
  `listAuditEvents(limit, offset)`/`countAuditEvents` on both adapters, `TicketService::
  listNotificationsPaged`/`listAuditEventsPaged`, and both existing routes now accept optional
  `page`/`pageSize` (same D126 contract as tickets) -- purely additive, since both routes already
  responded with an `{items: [...]}` envelope that any existing caller reading only `.items` keeps working
  against unchanged. The admin Audit log screen gained Previous/Next controls, mirroring the Backlog
  screen; the notification bell panel is left as a capped most-recent-200 read, matching how every other
  non-Backlog list in the app already behaves.

New test coverage: `tests/sqlite_integration_tests.cpp` asserts `countNotifications`/`countAuditEvents`
agree with their unpaginated counterparts, that the paginated overloads honor limit/offset without
overlapping or reordering pages, and that an offset past the end returns an empty page rather than an
error. Verified: full rebuild and `ctest` clean in all three build configurations; live-verified over real
HTTP against a fresh PostgreSQL database (a second real user receiving three `assigned` notifications from
ticket creation, confirming correct paging and that `unread` composes with `page`/`pageSize`; audit-log
pagination exercised against events the verification session's own user-creation calls produced); a full
Playwright/Chromium browser pass against a fresh SQLite database (11/11 checks: the shortcuts modal
opens/closes on `?`/Escape, `/` focuses the global search box, both quick-filter chips render and toggle
on Board and Backlog, board card Left/Right keyboard navigation moves focus between columns, and the audit
log's pagination bar renders with "Previous" correctly disabled on page 1). Full detail in
`docs/VERIFICATION.md`.

**Post-V1 batch 13 (done, 2026-08-03):** custom fields (D9, deferred-after-V1) -- item 6 from the same
menu batch 12 came from. Admin-defined fields scoped to a single project, shown on ticket create/edit/view.

- **Scope.** Deliberately cut from D9's full target ("fields support project/issue-type contexts, ordering,
  required/hidden settings, defaults, and show-on-create/edit/view flags"): one context per field (its
  project, not also its issue type -- D9 already rules out named screens/screen schemes, and per-issue-type
  contexts on top of that would multiply this batch's scope well past what was asked for), a fixed
  always-shown-everywhere visibility (no per-stage hidden/show-on flags), and no per-field default value.
  Six field types: text, number, date, checkbox, single_select, multi_select. Every value is a single
  string on the wire and in storage, even for multi_select (comma-joined) -- the same convention `labels`
  already uses, chosen to avoid a second "value is sometimes an array" JSON shape only custom fields would
  need.
- **Backend.** New `custom_fields` (the field catalog, `UNIQUE(project_id, name)`) and
  `ticket_custom_field_values` (`ON DELETE CASCADE` on both columns) tables (migration
  `018_custom_fields.sql`). New `IDatabase::listCustomFields`/`createCustomField`/`findCustomFieldById`/
  `editCustomField`/`deleteCustomField`/`listTicketCustomFieldValues` on both adapters, mirroring the
  existing `ProjectComponent` (D19) methods almost exactly. Field values are deliberately **not** embedded
  into the main `tickets` row/`TicketSelect` query the way `labels`/`component_id` are -- a project's
  custom fields are dynamic and per-project, not a small fixed global catalog, so folding them into the
  already-complex ticket read query would be high-risk for comparatively little benefit. Values are set
  only as part of `createTicket`/`editTicket` (`Domain::CreateTicketRequest`/`EditTicketRequest::
  customFieldValues`, full-replacement on edit, exactly like `labels`), applied transactionally via a
  private per-adapter helper (`applyTicketCustomFieldValues`, internal-linkage free function taking the
  live connection/database handle -- not part of `IDatabase`, since nothing outside `createTicket`/
  `editTicket` ever needs to call it directly). New `GET`/`POST`/`PATCH`/`DELETE
  /api/v1/projects/{key}/custom-fields[/{id}]` (project-Admin-or-above to write) and
  `GET /api/v1/tickets/{key}/custom-fields` (standard read access, one entry per field defined on the
  ticket's project, `value: null` if never set) routes. `TicketService::requireCustomFieldsSatisfied`
  rejects a create/edit missing a value for a `required` field.
- **Frontend.** A new "Custom fields" admin modal on the Projects screen (mirrors the existing Components
  modal structure closely). Dynamic inputs in the create-ticket modal and the ticket drawer's edit form,
  sharing `customFieldInputMarkup`/`collectCustomFieldValues` helpers (one function per direction: render
  the right input type, and read whatever's in it back out -- multi_select needs special handling both
  ways since a `<select multiple>` doesn't behave like every other input for `FormData`). A new "Custom
  fields" sidebar panel in the ticket drawer, shown only when the ticket's project actually has fields
  defined, displaying current values (view mode) or the same editable inputs (edit mode).

Two real bugs were found and fixed during this batch's own verification, both pre-existing and unrelated
to custom fields:

1. **A nondeterministic SQLite integration test.** The existing History-tab test block searched for "the
   assignee history entry" via a plain `find_if(fieldName == "assignee")`, but the test's own scenario
   produces *two* `assignee`-field history rows (one editTicket sets it, a later one clears it) that can
   tie on `created_at` (same wall-clock second) -- when they do, `ORDER BY created_at DESC, id DESC` breaks
   the tie by comparing random UUIDs, so which row `find_if` lands on first is effectively a coin flip.
   This is almost certainly the real cause of the "transient" `ticket-hub-sqlite-integration-tests`
   failures noted in earlier verification passes (previously guessed to be a `/tmp` file race between
   concurrent build configs) -- caught this time because it failed consistently within a single run rather
   than intermittently across separate ones. Fixed by searching for the specific entry whose `newValue`
   matches the assignment the test actually cares about, instead of "whichever sorts first."
2. **A CSS overflow.** `.project-card-actions` had no `flex-wrap`, so a project card's action-button row
   silently clipped its last button once a project had enough of them -- unnoticed until this batch's new
   "Custom fields" button became the fifth, pushing "Delete" half off the card. Fixed with `flex-wrap: wrap`.

New test coverage: `tests/sqlite_integration_tests.cpp` covers full custom-field-definition CRUD (creation,
duplicate-name rejection, invalid-`fieldType` rejection via the `CHECK` constraint, JSON options
round-tripping, sort-order assignment/editing), setting/reading/clearing ticket values through
`createTicket`/`editTicket` (including full-replacement clearing an omitted field), rejecting an unknown
field id, and cascade-delete of stored values when a field definition is deleted. Verified: full rebuild
and `ctest` clean in all three build configurations; live-verified over real HTTP against a fresh
PostgreSQL database (field CRUD, required-field-missing 400, ticket creation/edit with values, field
deletion cascading away a ticket's stored value); a full Playwright/Chromium browser pass against a fresh
SQLite database (11/11 checks) -- re-run against a genuinely fresh single server instance after an earlier
run's result was caught as untrustworthy (a leftover server process from a prior verification attempt had
kept accumulating state across supposedly-fresh database directories, since a later server start had
silently failed to bind the already-in-use port). Full detail in `docs/VERIFICATION.md`.

**Post-V1 batch 14 (done, 2026-08-03):** outbound webhooks (D39/D41) and outbound email (D52) -- the last
two items from the same menu batches 12/13 came from ("implementuj prosim 1 3 4 6 11 12" -- items 11 and
12).

- **Delivery architecture.** `CLAUDE.md` requires durable side effects to eventually use jobs/outbox/events
  rather than detached in-memory tasks, and this app has no background worker process. Split the same way
  `migrate`/`backup`/`restore`/`seed-demo` already are: request handlers only ever write a fast, local,
  durable outbox row (zero outbound network I/O in the request path); a new `ticket-hub-cli process-outbox`
  command, intended to be admin-cron-scheduled every 1-5 minutes, is the *only* place in the entire
  codebase that makes an outbound network call. New migration `019_outbox_delivery.sql` adds
  `webhook_subscriptions`, `webhook_deliveries`, and `email_deliveries` -- two separate concrete tables,
  not one generic "outbox_events" table, matching this codebase's existing preference for purpose-specific
  tables over a generic wrapper.
- **Outbound webhooks.** Global-administrator-managed subscriptions (`GET`/`POST /api/v1/webhooks`,
  `DELETE /api/v1/webhooks/{id}`), each with a generated signing secret shown exactly once at creation
  (matching how personal access tokens already work), an optional single-project filter, and an optional
  event-type filter (empty = every event) from a fixed catalog: `ticket.created`, `ticket.status_changed`,
  `ticket.updated`, `comment.added` -- deliberately scoped down from D41's full type/status/priority/
  assignee/custom-field visual filter matrix. Payloads are hand-built JSON in `TicketService` (no new
  `crow::json` dependency inside `application/`, which must not depend on `web/`), signed with
  `X-TicketHub-Signature: sha256=<hex-hmac>` (GitHub/Stripe-style) using a new `Common::hmacSha256Hex`,
  built on an extended version of the project's own existing hand-rolled SHA-256 rather than adding an
  OpenSSL/libcrypto dependency for one construction -- verified against RFC 4231 test vectors 1, 2, and 6.
  Every delivery attempt sets `CURLOPT_FOLLOWLOCATION = 0` so a compromised/malicious webhook target can
  never redirect the signed payload to an unintended host (SSRF defense).
- **Outbound email.** A pluggable SMTP backend (`TICKETHUB_SMTP_HOST`/`_PORT`/`_USERNAME`/`_PASSWORD`/
  `_FROM`/`_USE_TLS`) for the existing fixed in-app notification set (D14) -- email enqueueing mirrors the
  in-app notification set one-for-one, gated only on installation-wide SMTP configuration (no per-user
  notification-preference toggle; D86 already ruled those out elsewhere). Subject and recipient address are
  `stripCrLf()`-sanitized before building the raw SMTP message, since the subject is built from
  user-controlled `ticket.summary` and an unsanitized value would allow email header injection (e.g. a
  forged `Bcc`).
- **New dependency: libcurl**, linked only into `ticket-hub-cli` (not `ticket-hub-core` or the `ticket-hub`
  server binary) -- supports both HTTP and SMTP through one library, keeping the server's own dependency and
  attack surface unchanged. Fixed retry policy (not exponential): `Domain::MaxDeliveryAttempts = 10`,
  `Domain::DeliveryRetryDelayMinutes = 5`; an exhausted delivery is marked permanently `failed` (terminal,
  no manual-retry API in this batch). Webhook signing secrets and SMTP credentials are a deliberate,
  documented exception to "store token/session verifiers as hashes": HMAC signing needs the raw key at send
  time (unlike a bearer-token comparison), so the secret is stored in cleartext the same way the database
  connection string itself already is. `Dockerfile` updated to install `libcurl4-openssl-dev` in the build
  stage and `libcurl4` in the runtime stage.

A real bug was found and fixed during this batch's own live-delivery verification, outside webhook/email
logic itself: `SqliteDatabase::recordWebhookDeliveryResult`/`recordEmailDeliveryResult`'s `UPDATE`
statements mixed an anonymous `?` placeholder for `last_error` with explicit numbered placeholders
(`?1`/`?3`/`?4`) for the other columns. SQLite assigns an anonymous `?` the next index after the largest
*explicit* index appearing to its left in the SQL text -- since `last_error = ?` appeared before any
numbered placeholder, it was silently assigned index 1, colliding with `WHERE id = ?1`, while the C++
code's `bind(2, error)` bound to an index nothing in the query referenced. The practical effect: every
failed delivery's `last_error` column stored the delivery's own id instead of the actual error message.
Caught by an ad hoc verification query showing a UUID-shaped `last_error` value instead of "Couldn't
connect to server"; the PostgreSQL adapter was unaffected (its `$1`/`$2`/... placeholders are always
explicit). Fixed by making the SQLite placeholder explicit (`?2`) in both methods; a new regression
assertion in `tests/sqlite_integration_tests.cpp` (via a new `scalarText` test helper) checks `last_error`'s
actual text content for both the webhook and email failure paths, which the original test block did not do.

New test coverage: `tests/sqlite_integration_tests.cpp` covers webhook subscription CRUD (secret
generation, event-type/project-filter round-tripping), delivery enqueue/list/record-result for both the
success and exhausted-retry paths (including the `last_error` content regression check above), cascade-
delete of a subscription's deliveries (`ON DELETE CASCADE`), and the equivalent email-delivery lifecycle;
`tests/crypto_tests.cpp` gained 4 RFC 4231 HMAC-SHA256 vector tests. Verified: full rebuild and `ctest`
clean in all three build configurations. Live-verified end-to-end over real HTTP/SMTP against both a fresh
SQLite database and a fresh PostgreSQL database: a real local HTTP receiver and a real local SMTP receiver
(`aiosmtpd`) confirmed the compiled `ticket-hub-cli process-outbox` binary actually delivers -- correct
HMAC-SHA256 signature (independently recomputed in Python), correct email content -- and marks both rows
`delivered`/`sent`; a second subscription pointed at an unreachable target, and an intentionally-unset SMTP
host, confirmed the failure/retry path (`last_error` populated correctly, `attempt_count` incrementing,
terminal `failed` status at `MaxDeliveryAttempts`) on both databases. A Playwright/Chromium browser pass
against a fresh SQLite database covered the new admin "Webhooks" screen: creating a subscription
(secret-once banner), the subscription list, and delete. Docker daemon was not available in this sandbox
environment, so the updated `Dockerfile`'s build could not be executed here -- the `libcurl4-openssl-dev`/
`libcurl4` additions were reviewed by inspection only; a real Docker build should be run once network/daemon
access is available. Full detail in `docs/VERIFICATION.md`.

**Post-V1 batch 15 (done, 2026-08-03):** REST write idempotency keys (D128, deferred-after-V1) -- offered
as one item on a follow-up menu after batch 14 closed the original six-item list ("jaka dalsi mozna
vylepseni?" -- "what other possible improvements?"); the user picked exactly this one ("pouze 1").

- **Scope.** Opt-in per request: only a request that actually carries an `Idempotency-Key` header is ever
  looked up or cached, every other request is entirely unaffected. Wired into exactly five POST routes that
  create a new, independently visible resource -- `POST /api/v1/tickets`, `POST /api/v1/projects`,
  `POST /api/v1/tickets/{key}/comments`, `POST /api/v1/tickets/{key}/worklogs`,
  `POST /api/v1/tickets/{key}/clone` -- not a generic mechanism applied to every write route: PATCH/DELETE/
  bulk/status-change routes already converge to the same end state on repeat, so there is no duplicate
  *record* for a key to prevent there.
- **Backend.** New `idempotency_keys` table (`PRIMARY KEY (user_id, idempotency_key)`, migration
  `020_idempotency_keys.sql`) and `IDatabase::findIdempotencyRecord`/`recordIdempotencyResult` on both
  adapters (the latter deliberately best-effort -- `INSERT OR IGNORE` / `ON CONFLICT DO NOTHING` -- so a
  narrow concurrent-retry race never surfaces as a 500), with thin `TicketService` pass-throughs. New
  `Api.cpp` helpers `idempotencyReplay`/`recordIdempotentResult`: a previously-unseen key proceeds normally
  and its 2xx response is cached afterward (a non-2xx response is never cached, so a corrected retry after
  a validation failure just runs normally); a key whose cached `request_hash` (SHA-256 of the route path
  plus the raw body -- not just the body, so the same key reused across two different routes/tickets can
  never be mistaken for a legitimate retry even if the bodies coincidentally match) matches gets that
  response replayed (`Idempotency-Replayed: true`); a mismatch gets `409`.
- **Frontend.** The demo web UI's five equivalent forms/buttons send a fresh `crypto.randomUUID()` via a
  new `idempotencyHeaders()` helper on every submit, and each submit control is disabled for the duration
  of its request -- two complementary defenses, not one: the button-disable is what actually stops a
  literal double-click (the key alone can't, since a fresh one is minted per handler invocation), while the
  key covers the case a double-click guard can't -- a successful response that never reaches the browser,
  followed by a manual retry once the control re-enables.
- **A real pre-existing bug was found and fixed while wiring the frontend**, unrelated to idempotency logic
  itself: `web/app.js`'s shared `api()` helper built `fetch(path, { headers, ...options })` -- spreading
  `...options` *after* the carefully-merged `headers` object meant any caller that itself passed an
  `options.headers` key would silently clobber that merge, dropping `Content-Type`/`X-CSRF-Token` entirely.
  Invisible until this batch's `idempotencyHeaders()` became the first caller ever to pass one: every wired
  create action started failing with `403 Missing or invalid CSRF token`. Caught immediately via a live
  Playwright verification pass (not shipped); fixed by reordering to `fetch(path, { ...options, headers })`.

New test coverage: `tests/sqlite_integration_tests.cpp` covers a lookup miss (`nullopt`), a stored record
round-tripping its hash/status/body exactly, the same key value used by two different users being unrelated
records, and the best-effort duplicate-write semantics (a second write for an already-recorded key is
silently ignored). Verified: full rebuild and `ctest` clean in all three build configurations. Live-verified
over real HTTP against both a fresh PostgreSQL database and a fresh SQLite database: the happy-path replay
(identical second response, no duplicate row -- confirmed via both the API and a direct database count),
the same-key-different-body 409, the cross-route collision defense, the oversized-key 400, and the
failed-attempts-are-never-cached case. A full Playwright/Chromium browser pass against a fresh SQLite
database exercised all five wired UI actions end-to-end with no console errors and no duplicate records,
confirmed the submit-button-disabled behavior, and is what caught the `api()` bug above. Full detail in
`docs/VERIFICATION.md`.

**Post-V1 batch 16 (done, 2026-08-05):** bug fix, user-reported ("kde najdu archived projects a jak udelat
unarchive" -- "where do I find archived projects and how do I unarchive one").

- **The bug.** Archiving a project made it disappear from the web UI with no way back. `GET
  /api/v1/projects` -- the only project list the UI ever fetched -- excludes archived projects by design
  (D87, correct behavior), but there was no substitute view anywhere: the recycle bin only holds
  soft-deleted projects, not archived ones, and there was no second API route for archived projects
  (unlike the recycle bin's `/api/v1/projects/deleted`). Compounding it, `projectJson()` never serialized
  the `archived` field at all, so the pre-existing per-card Archive/Unarchive button's `project.archived`
  check was always `undefined` client-side -- dead code even before the missing-view problem.
- **Backend.** New `IDatabase::listArchivedProjects()` on both `SqliteDatabase`/`PostgresDatabase`,
  mirroring the existing `listDeletedProjects()`/shared `ProjectSelectSql` pattern. New
  `TicketService::listArchivedProjects(actor)` uses `requireReadAccess` -- the same rule as `listProjects`
  (any authenticated reader, or anonymous if that toggle is on) -- deliberately not
  `requireGlobalAdmin` like `listDeletedProjects`, since D87 says archiving "leaves active lists" but the
  project stays viewable, unlike the recycle bin. New `GET /api/v1/projects/archived` route.
  `projectJson()` now includes `archived`.
- **Frontend.** `web/app.js`'s `renderProjectsView` changed from a `showingDeleted` boolean to a
  `viewMode` string (`'active' | 'archived' | 'deleted'`). The Projects page gained a "📦 Archived" toggle
  next to the existing admin-only "🗑 Recycle bin" toggle (visible to any reader, matching the new route's
  access rule); archived-view cards show a working "Unarchive" button.
- **Tests.** Extended the existing D87 block in `tests/authorization_integration_tests.cpp`: a non-admin
  reader sees an archived project via `listArchivedProjects` with `archived == true`; unarchiving removes
  it from that list again.
- **Verified:** full rebuild and `ctest` clean (8/8) in the SQLite + Crow-server configuration. PostgreSQL
  could not be compiled in this environment (only the runtime `libpq5` package is installed, not
  `libpq-dev`) -- the Postgres change mirrors the existing pattern structurally but is unverified by a real
  compile here. Live end-to-end over real HTTP against the actual compiled server and a demo-seeded SQLite
  database (the Chrome browser extension needed for a Playwright pass was not connected in this
  environment, so `curl` was used instead against the same routes the UI calls): archiving a project
  removed it from `GET /api/v1/projects` and surfaced it in `GET /api/v1/projects/archived` with
  `archived:true`; unarchiving reversed both; a non-admin user got `403` archiving a project they don't
  administer but `200` (not `403`) reading the archived list, confirming the read-access-not-admin-gated
  design over the wire, not just in the unit test. Full detail in `docs/VERIFICATION.md`.

## Verification status

Core, CLI, all eight test binaries, and the `ticket-hub` server target itself all compile and pass/run
cleanly on both SQLite and PostgreSQL, in every supported build configuration, including a live HTTP
smoke test of essentially every route across all three completed phases and, across twelve batches, a
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
becomes clickable), simplified worklogs (logging time shows the correct formatted duration and
comment, deleting an entry removes it, an unparseable duration is rejected before it reaches the server),
and now the admin/security audit log (the nav item's visibility correctly gated by admin status, and
audit rows produced with the correct action/actor after an admin action). **Phase 4 (Collaboration) is
now fully implemented and fully verified.** The long-standing "server target unverified because
`github.com` is unreachable" limitation recorded in every prior session no longer applies in this
environment, and there is no longer a gap between what the
API exposes for Phases 1-3 (plus the comment-editing, reactions, mentions/notifications, Markdown-
rendering, and worklog slices of Phase 4) and what the demo UI can reach.
`findCommentById`/`editComment`/`deleteComment` gained dedicated SQLite-integration coverage (success,
version-increment, `edited_at` set, stale-version conflict, unknown-comment no-op) and
authorization-integration coverage (non-author-non-admin Forbidden, self-edit succeeds, global-admin can
edit/delete any comment); `addCommentReaction`/`removeCommentReaction`/`listCommentReactions` gained the
same three-layer coverage (SQLite-integration idempotency/listing, authorization-integration no-project-
role/unknown-key/unknown-comment rejection, and a live-PostgreSQL smoke test); `findUserByHandle` and the
five `notifications` methods gained the same three-layer coverage plus identity-integration coverage for
handle normalization/uniqueness/format validation on `create-user`. Each Phase 5 slice (filter/search
widening, personal dashboard, Kanban WIP limits, and now the full attachments vertical) added its own
SQLite-integration, authorization-integration, live-PostgreSQL, and browser-verification coverage on top
of that. **Phase 5 is now fully implemented and fully verified, closing out Milestone 2** -- attachment
upload/download/delete/recycle-bin, all four native-element previews, and full Markdown-editor drag-drop/
paste integration were all exercised through a real browser, including real native `drop`/`paste` DOM
events (not just `setInputFiles`) both on the dedicated dropzone and directly on the Markdown editor.
Full detail, including exactly what was exercised (and the several real bugs this browser testing and
test-writing caught and fixed along the way, across every batch this session), is in
`docs/VERIFICATION.md`.
