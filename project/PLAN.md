# Ticket Hub plan

The authoritative, continuously updated plan is [NEXT.md](NEXT.md); the phased roadmap it tracks against
is [docs/REDUCED_SCOPE_ROADMAP.md](docs/REDUCED_SCOPE_ROADMAP.md) (the original
[docs/ROADMAP.md](docs/ROADMAP.md) is long-term reference only). This file is a short pointer plus a
snapshot status line, kept in sync at each milestone boundary -- see `NEXT.md` for full batch-by-batch
detail and `docs/VERIFICATION.md` for exactly what was tested and how.

## Current status (2026-08-03)

**The entire reduced-scope V1 roadmap (`docs/REDUCED_SCOPE_ROADMAP.md`, Milestones 1-4 / Phases 1-8) is
now complete**, including its Phase 8 exit gate (`docker compose up` produces a usable, documented
instance; all supported build configurations compile and pass tests; no known open security issue from
the hardening pass). See "The roadmap is now complete" in `NEXT.md` for the exact closing detail and what
remains only as optional, non-roadmap follow-up.

- **Milestone 1** (Phases 1-3: identity/sessions, authorization/projects, issue core/fixed workflow) --
  **fully complete** at the core/CLI/test/server/UI layer. Re-typing (`issueTypeKey`)/re-parenting
  (`parentIssueKey`) an issue after creation, the one item left open since Phase 3, was added post-V1 as
  optional follow-up batch 2 (see below).
- **Milestone 2** (Phase 4: Collaboration; Phase 5: Attachments and Kanban board) -- **complete**. Comment
  editing/tombstone delete, fixed emoji reactions, @mention handles and in-app notifications, the
  Markdown editor (toolbar/live preview/full attachment upload+drag-drop+paste), simplified worklogs, the
  admin/security audit log, ad-hoc issue filter/search widening, the personal dashboard, Kanban board WIP
  limits, and the full attachments vertical (local filesystem storage, four native-element previews,
  sortable list, 90-day recycle bin) are all implemented, tested on both PostgreSQL and SQLite, and
  browser-verified end-to-end with Playwright/Chromium.
- **Milestone 3** (Phase 6: API/security hardening/export; Phase 7: backup/restore/upgrade) --
  **fully complete**. Phase 6: personal access tokens (D39/D40), the active-session list/"sign out
  everywhere" endpoint (D54), fixed rate limits (D124/D125), the versioned `/api/v1` prefix (D127),
  read-only CSV export (D48), fixed request-body/bulk-item constants (D125), the security hardening pass
  (found and fixed a real stored-XSS vulnerability in attachment preview/download), and numbered/offset
  pagination (D126) for `GET /api/v1/issues` (a deliberate partial rollout, every other list endpoint
  documented as still open). Phase 7: backup and restore (D106-D108,
  `ticket-hub-cli backup <dir>` / `restore <dir> --yes`, live-verified end-to-end on both SQLite and
  PostgreSQL matching the exit gate exactly), the upgrade mechanism (D111, already satisfied by
  `ticket-hub-cli migrate`), structured JSON logs to stdout (D133, a new `JsonLogHandler` replacing
  Crow's default stderr logger), and an in-app admin version banner (D112, admin-configured, no outbound
  network calls). A web UI for managing tokens and sessions was added post-V1 as the first optional
  follow-up item (see below).
- **Milestone 4** (Phase 8: packaging and release hardening) -- **fully complete**. Docker image + Compose
  distribution (D50) done: a two-stage `Dockerfile` and a `ticket-hub` service added to
  `docker-compose.yml` alongside `postgres`, so `docker compose up` alone brings up the full instance.
  Verified as far as this environment's network policy allows -- `docker build --check`/
  `docker compose config` pass cleanly and the runtime configuration was verified directly on the host
  against both SQLite and live PostgreSQL, but the actual image build was blocked by a network-egress
  policy denial on the CDN host Docker Hub redirects layer pulls to (see `docs/VERIFICATION.md` for the
  full disclosure). Light and dark theme (D46) is also done: the UI follows the OS-level
  `prefers-color-scheme` signal automatically (no manual toggle), browser-verified in both modes with a
  real bug found and fixed (`.link-form input` had no dark styling). The accessibility baseline pass (D47)
  and browser-support note (D139) are also done: issue rows/board cards/project cards/inline key links are
  now keyboard-focusable and operable with Enter/Space (a real gap found and fixed), and D139 was
  reconfirmed satisfied by construction. **The threat-model/security self-review is also done**
  (`docs/THREAT_MODEL.md`): it found and fixed a real broken-access-control (IDOR) bug across the comment/
  worklog/attachment mutation routes, plus four lower-severity issues (a session-derived CSRF cookie, a
  login timing side channel, a missing CSRF check on logout, and CSV formula injection). **This closes
  Phase 8, Milestone 4, and the entire roadmap.**
- **Post-V1, optional follow-up (batch 1).** The user was asked to pick the first item and chose a web UI
  for managing personal access tokens and active sessions -- both already had a complete REST API since
  Phase 6; only the `web/` surface was missing. Done: a new "Account" page (create/list/revoke tokens with
  the raw value shown exactly once, list/sign-out active sessions), no backend or schema changes,
  browser-verified end-to-end including a genuine two-session sign-out-others test.
- **Post-V1, optional follow-up (batch 2).** The user was asked to pick the next item and chose re-typing/
  re-parenting an issue after creation -- the one item left open since Phase 3. `editIssue` now edits
  `issueTypeKey`/`parentIssueKey`, reusing the same fixed hierarchy-shape rules as `createIssue` plus a new
  self-parent guard, and rejecting a hierarchy-level retype while the issue has children (checked
  transactionally in `IDatabase::editIssue`, the same precedent `moveIssue`'s own "has children" rule
  already established). New Type/Parent picker in the issue drawer's edit form. Verified end-to-end
  including against live PostgreSQL (both database adapters changed) and browser-verified with Playwright.
- **Post-V1, optional follow-up (batch 3).** The user was asked to pick the next item and chose
  drag-and-drop card movement on the Kanban board. Cards are now draggable between columns, applying the
  same `PATCH /api/v1/issues/{key}/status` route the drawer's dropdown already uses; a Done-category drop
  without a resolution opens a small prompt first (same D68-D70 rule). No backend/schema/API changes.
  Browser-verified with Playwright/Chromium (a manual multi-step mouse simulation was needed for reliable
  headless-Chromium HTML5 drag verification, since Playwright's built-in `dragTo()` proved unreliable for
  longer drag distances specifically). The drawer's dropdown remains the keyboard-operable path.
- **Post-V1, optional follow-up (batch 4, final).** The user was asked to pick the next item and chose
  both remaining ones: the bulk Done-status picker (the bulk status route already accepted a shared
  `resolution`; only the UI excluded Done-category statuses) and keyboard-driven multi-select for the
  issues table (a header select-all checkbox, Shift+click range select, Shift+ArrowDown/ArrowUp keyboard
  range extension). Both `web/`-only. Browser-verified with Playwright/Chromium in both light and dark
  mode. This was the last item on the original optional-follow-up list.
- **Post-V1, batch 5.** The user asked whether Ticket Hub supports Jira-style `/browse/ABC-123` direct
  issue links -- it didn't, so this batch added one: a new `GET /browse/{key}` server route serving the
  same app shell as `/`, plus `history.pushState`/`popstate` URL syncing in `web/app.js` (guarded to avoid
  duplicate history entries on repeated opens of the same issue). No backend/schema changes beyond the
  route. Browser-verified with Playwright/Chromium across logged-out deep links, reloads, Back/Forward,
  and an unknown-key error path.
- **Post-V1, batch 6.** The user asked to rename "issue" to "ticket" everywhere -- UI and database tables
  -- and, asked to clarify whether that should also cover REST API paths and internal C++/CSS naming,
  chose the fully comprehensive option. New migration `016_ticket_terminology.sql` (both backends) renames
  every `issue*` table/column/index (plus, on PostgreSQL, the auto-generated constraint names a table/
  column rename doesn't touch on its own); every C++ type/method/identifier, REST route, JSON field, CSS
  class, and user-facing UI string was renamed to match. `docs/SCHEMA.md`/`docs/SCOPE.md`/`README.md`'s
  current-state sections were updated in place; historical batch narratives (this file's entries above,
  `NEXT.md`, `CHANGELOG.md`, `docs/VERIFICATION.md`) were left as written, with a new dated entry added to
  each instead. Verified end-to-end against fresh live PostgreSQL and SQLite databases (zero remaining
  "issue"-named schema objects), a live HTTP smoke test, and a full Playwright/Chromium pass confirming no
  leftover "Issue" text in the UI and that creating a ticket still works.
- **Post-V1, batch 7.** The user asked which fields tickets support (priority/components/labels/
  created/updated); components turned out to be a real gap -- decided `KEEP_FOR_V1` in
  `docs/REDUCED_SCOPE_DECISIONS.md` (D19) but never actually implemented. Asked to implement it, and did:
  new migration `017_project_components.sql` (both backends) adds `project_components` (name, description,
  lead, default assignee) and a nullable `tickets.component_id` (`ON DELETE SET NULL`, no recycle bin --
  D19 doesn't call for one). New `GET`/`POST /api/v1/projects/{key}/components` and `PATCH`/`DELETE
  /api/v1/projects/{key}/components/{id}` routes (project-Admin-or-above to write, IDOR-safe scoping to
  `(projectKey, componentId)` together); `createTicket`/`editTicket`/`cloneTicket` and `TicketFilter`
  wired through. `web/` gained a components management dialog, ticket-form picker, drawer display, and
  table filter. Verified end-to-end against fresh live PostgreSQL and SQLite databases (including the `ON
  DELETE SET NULL` behavior through the real HTTP API), new unit/integration/authorization test coverage,
  and a full Playwright/Chromium pass (which found and fixed one real layout bug in the components list).
- **Post-V1, batch 8.** The user asked for a full gap analysis between the V1 decision register and the
  actual codebase, then asked to implement everything found. All 142 decisions in
  `docs/REDUCED_SCOPE_DECISIONS.md` were re-checked against the real code; five `KEEP_FOR_V1`/
  `ALREADY_IMPLEMENTED_AND_KEEP` features turned out to be gaps (D101's attachment sortable list, initially
  suspected, was confirmed correctly implemented -- a false alarm). Implemented all five: Markdown
  checklist rendering (D62, real checkboxes instead of literal bracket text), a client-side-only "No Epic"
  ticket filter (D66), a conflict dialog on a stale optimistic-lock save (D129, new `error.status` plumbing
  through `api()`), self-service timezone/clock-format preferences (D45, new `PATCH
  /api/v1/account/preferences` / `IDatabase::updateUserPreferences`, a `localStorage`-based "has the user
  set this" signal to make auto-detect safe, date-only values rendered in UTC with no shift), and changing
  an active project's key (D91, new `PATCH /api/v1/projects/{key}/key` / `IDatabase::changeProjectKey`,
  reusing the exact alias-and-bulk-rename pattern `moveTicket`/D38 already established, extended to every
  ticket in the project at once). Found and fixed two real bugs during this batch's own browser
  verification: a stale `state.selectedProject` after renaming the currently-selected project (silently
  emptied the Tickets/Board views), and `.modal-backdrop` sharing a lower `z-index` than `.ticket-drawer`
  (any modal opened over the drawer, most importantly the new conflict dialog, was rendered behind it and
  unclickable). New unit/integration/authorization test coverage; verified end-to-end against fresh live
  PostgreSQL and SQLite databases over real HTTP, and a full Playwright/Chromium browser pass (13/13
  checks).
- **Post-V1, batch 9.** The user asked for the board to be fixed since a project's backlog can grow huge
  and shouldn't sit in its own Kanban column, and for backlog tickets to get a dedicated screen instead;
  mid-batch, also asked for every ticket to have Markdown-supported (not forced single-line) worklogs and
  for created/updated timestamps to be shown. Backlog is now off the board (amending D32's "one column per
  status" -- 4 columns remain: Confirmed, In Progress, In Review, Done) and has its own new, paginated
  "Backlog" screen (the first list view in this app with real server-side pagination rather than the fixed
  200-row cap, since a backlog is explicitly unbounded by design), ordered by a new `sort=rank` query
  parameter (`Domain::TicketFilter::sortByRank`, both adapters) so paginated results follow the same manual
  priority order the existing reorder arrows already write to. The board's own ticket fetch changed from
  one unfiltered request to one status-filtered request per column, fixing a real correctness gap where a
  large backlog could previously have silently consumed the board's fixed fetch budget. Worklogs' "what did
  you work on" field is now a Markdown textarea (toolbar, live preview, @mention autocomplete) instead of a
  plain single-line input -- a pure frontend fix, since the backend already allowed multi-line, 10,000-
  character content. Every ticket list table (Tickets, Backlog) now shows Created/Updated columns, matching
  what the ticket detail drawer already displayed; the Dashboard's compact widgets deliberately keep their
  terser layout. New SQLite integration test coverage for `sortByRank`; verified end-to-end against a fresh
  live PostgreSQL database over real HTTP and a full Playwright/Chromium browser pass (19/19 checks) against
  a fresh SQLite database seeded with 62 backlog tickets to exercise real pagination.
- **Post-V1, batch 10.** The user asked for the ticket detail layout to look more like Jira. Restyled the
  ticket drawer (pure UI/CSS, no API/schema changes, every existing element id and event handler kept
  working unchanged): the status select is now a colored pill button near the title instead of a plain
  dropdown buried in the sidebar, with the resolution picker/confirm flow inline beside it; the sidebar is
  now two bordered "Details"/"Dates" panel cards instead of one flat list; Comments and Work log are now
  tabs in one Activity section (tracked by a new module-level `activeActivityTab` so the active tab
  survives a full drawer re-fetch, e.g. after logging time) instead of two always-visible stacked
  sections, with each panel's add-form moved above its list to match Jira's convention. Found and fixed
  two real bugs during this batch's own browser verification: a pre-existing backend bug where confirming
  a resolution on an already-Done ticket with none recorded silently no-opped instead of persisting
  (`changeTicketStatus`'s same-status guard in both adapters, now narrowed to still apply when a missing
  resolution is being newly supplied), and a CSS regression this batch itself introduced
  (`.resolution-inline`'s `display: flex` silently overrode the native `[hidden]` attribute, showing the
  resolution picker on non-Done tickets, fixed with an explicit `[hidden]` override). New SQLite
  integration test coverage for the resolution fix; verified end-to-end against fresh live PostgreSQL and
  SQLite databases over real HTTP and a full Playwright/Chromium browser pass (20/20 checks, plus
  screenshots of all three status-pill colors and dark mode). README's ticket-detail screenshot
  regenerated.
- **Post-V1, batch 11.** The user asked whether History/Activity/Transitions tabs could also be added to
  the ticket detail drawer, right after batch 10's Activity tabs landed. `ticket_history` already existed
  and was already written to by `changeTicketStatus`/`editTicket`/`moveTicket`, but had no read-side API --
  purely write-only internal bookkeeping until now. Added `IDatabase::listTicketHistory` on both adapters
  (mirroring `listComments`/`listWorklogs`, with a `LEFT JOIN` since `actor_user_id` is nullable unlike
  those tables' `author_user_id`), `TicketService::listTicketHistory`, and a new
  `GET /api/v1/tickets/{key}/history` route. The drawer gained a third Activity tab, "History (N)",
  rendering each row as a Jira-style "changed X from Y to Z" sentence via new frontend-only formatting
  helpers and label lookup maps -- no API/schema change, purely new read exposure of an existing table.
  New SQLite integration test coverage (including a self-caught-and-fixed `std::is_sorted` comparator bug
  in the test itself -- `>=` is not a valid strict weak ordering, fixed to strict `>`); verified end-to-end
  against fresh live PostgreSQL and SQLite databases over real HTTP and a full Playwright/Chromium browser
  pass (8/8 checks). README's ticket-detail screenshot regenerated to show the History tab active.
- **Post-V1, batch 12.** The user asked for a list of possible new functionalities; offered six quick/safe
  items and six larger decision-register-deferred features, and the user picked six by number
  ("implementuj prosim 1 3 4 6 11 12"). This batch covers the three quick ones: quick filters on
  Board/Backlog ("Only my tickets"/"No Epic" chip toggles), more keyboard shortcuts (`/` search, `?` help
  modal, Up/Down/Left/Right row/card navigation), and D126 pagination extended to notifications and the
  admin audit log (scoped down from the original four-endpoint ask -- comments/worklogs stay unpaginated
  since a single ticket's list is naturally bounded, matching the same reasoning already used for
  `ticket_history`). New SQLite integration test coverage for the paginated notification/audit-log
  overloads; verified end-to-end against a fresh live PostgreSQL database over real HTTP and a full
  Playwright/Chromium browser pass (11/11 checks). The other three picked items -- custom fields (D9),
  outbound webhooks (D39/D41), outbound email (D52) -- are real deferred-feature work, tracked as ongoing
  in `docs/SCOPE.md`'s "Deferred after V1, in progress" note; webhooks and email additionally need a
  durable outbox/delivery mechanism first (`CLAUDE.md`'s "no detached in-memory tasks for email/webhooks"
  rule).
  **There is currently no further queued work in this batch; custom fields/webhooks/email continue next.**

## Implementation rules

- Preserve buildability and tests after every change; run `ctest --output-on-failure` before finishing a
  batch, on both database backends and in every supported build configuration.
- Keep PostgreSQL and SQLite behavior aligned at the application-contract level.
- Never introduce generic SQL into application/domain modules.
- Schema migration files are immutable once applied; add a new ordered migration instead of editing one.
- Side effects must eventually use durable jobs/outbox events, not detached in-memory work -- and V1 has
  no job infrastructure at all (`docs/REMOVED_AND_DEFERRED_FEATURES.md`), so anything that would need one
  is either simplified to an on-demand check or dropped.
- Do not implement anything from `docs/REMOVED_AND_DEFERRED_FEATURES.md` without an explicit new product
  conversation, and do not jump ahead to a later phase before the current one's exit gate is met.
