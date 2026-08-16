# Ticket Hub plan

The authoritative, continuously updated plan is [NEXT.md](NEXT.md); the phased roadmap it tracks against
is [docs/REDUCED_SCOPE_ROADMAP.md](docs/REDUCED_SCOPE_ROADMAP.md) (the original
[docs/ROADMAP.md](docs/ROADMAP.md) is long-term reference only). This file is a short pointer plus a
snapshot status line, kept in sync at each milestone boundary -- see `NEXT.md` for full batch-by-batch
detail and `docs/VERIFICATION.md` for exactly what was tested and how.

## Current status (2026-08-08)

**The entire reduced-scope V1 roadmap (`docs/REDUCED_SCOPE_ROADMAP.md`, Milestones 1-4 / Phases 1-8) is
now complete**, including its Phase 8 exit gate (`docker compose up` produces a usable, documented
instance; all supported build configurations compile and pass tests; no known open security issue from
the hardening pass). See "The roadmap is now complete" in `NEXT.md` for the exact closing detail and what
remains only as optional, non-roadmap follow-up.

## Approved reliability and scale-up work (2026-08-08)

The following work was approved after the post-V1 technical analysis. It is a
new operational-quality roadmap, separate from the completed reduced-scope V1
roadmap above. Items are ordered by delivery priority; an unchecked item is
not a claim that the capability already exists.

- [x] **P0 — CI and reproducible builds.** Added CMake presets, declared every
  build dependency, and run a clean SQLite test build, a PostgreSQL compile
  build, formatting/static checks, and browser checks in CI.
- [x] **P0 — Harden the production Docker deployment.** Split production and
  development Compose configuration; remove the default database password and
  public PostgreSQL port from production; document TLS reverse-proxy and
  request-size limits.
- [x] **P1 — Operable durable outbox.** Added an optional Compose worker
  schedule, an administrator delivery overview with failures, and a
  deliberate manual retry action. A systemd timer remains an equivalent
  deployment option documented in `docs/DEPLOYMENT.md`.
- [x] **P1 — Automated end-to-end and accessibility testing.** Added committed
  Playwright/axe coverage for authentication, authorization, ticket editing,
  attachments, drawer resizing, story-point choices, and ticket history.
- [x] **P1 — Reconcile decision documentation.** Marked custom fields,
  webhooks, outbound email, and idempotency as implemented post-V1 while
  preserving the historical deferred-scope decisions.
- [x] **P1 — Security release checks.** Added dependency, code, and container
  scanning in CI; generate an SBOM and document a recurring update cadence.
- [x] **P2 — Start splitting oversized modules.** The fixed estimation scale
  and ticket-drawer interaction controller are now native ES modules,
  removing their stateful DOM code from `web/app.js` without a framework or a
  bundler. Continue splitting API/state, board and backend route domains in
  future focused refactor batches without changing the public API.
- [x] **P2 — Scale larger installations.** Replaced ad-hoc ticket search with
  native SQLite FTS5/PostgreSQL full-text search; retain bounded board queries
  and add virtualization/selective pagination where measurements require it.
- [x] **P2 — Safer backup and restore.** Added a backup manifest with version,
  migration and attachment integrity data plus an explicit maintenance
  preflight before destructive restore.

**Latest verification (2026-08-08):** the current SQLite server build was
compiled from this checkout, started with an isolated demo database, and
manually exercised through the browser. The start page and the extracted
story-point and ticket-drawer modules all returned HTTP 200; the preview was
then stopped cleanly.

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
- **Post-V1, batch 13.** Custom fields (D9, deferred-after-V1) -- item 6 from batch 12's menu.
  Admin-defined fields (text/number/date/checkbox/single-select/multi-select) scoped to a single project,
  shown on ticket create/edit/view; scoped down from D9's full target (one context per field, no per-stage
  visibility flags, no default value). New `custom_fields`/`ticket_custom_field_values` tables (migration
  `018_custom_fields.sql`); `IDatabase::listCustomFields`/`createCustomField`/`editCustomField`/
  `deleteCustomField`/`listTicketCustomFieldValues` on both adapters (mirroring the existing
  `ProjectComponent` methods); values set only via `createTicket`/`editTicket`'s new `customFieldValues`
  field (full-replacement on edit, like `labels`), applied through a private per-adapter free-function
  helper rather than a public `IDatabase` method (an early draft got this wrong for Postgres -- see
  `docs/VERIFICATION.md`'s "Two pre-existing bugs" / "Design correction" notes); new
  `GET`/`POST`/`PATCH`/`DELETE /api/v1/projects/{key}/custom-fields[/{id}]` and
  `GET /api/v1/tickets/{key}/custom-fields` routes; `TicketService::requireCustomFieldsSatisfied` for
  `required` fields. `web/` gained a "Custom fields" admin modal (mirrors Components), dynamic inputs in
  the create-ticket modal and the drawer's edit form, and a new sidebar panel showing values. Found and
  fixed two pre-existing bugs unrelated to custom fields: a nondeterministic SQLite integration test
  (likely the real cause of previously-reported "transient" failures) and a `.project-card-actions`
  CSS overflow. New SQLite integration test coverage; verified end-to-end against a fresh live PostgreSQL
  database over real HTTP and a full Playwright/Chromium browser pass (11/11 checks, re-run after an
  earlier result was caught as untrustworthy due to a leftover server process).
- **Post-V1, batch 14.** Outbound webhooks (D39/D41) and outbound email (D52) -- items 11 and 12 from
  batch 12's menu, both needing a durable delivery mechanism first per `CLAUDE.md`. New
  `webhook_subscriptions`/`webhook_deliveries`/`email_deliveries` tables (migration
  `019_outbox_delivery.sql`); request handlers only ever write a durable delivery row, and a new
  `ticket-hub-cli process-outbox` command -- the only place in the codebase that makes an outbound network
  call -- delivers them, matching the existing `migrate`/`backup`/`restore` "explicit admin-run CLI step"
  pattern. Webhook payloads signed with a new hand-rolled `Common::hmacSha256Hex` (RFC 4231-verified,
  built on the project's existing hand-rolled SHA-256 rather than a new OpenSSL dependency); a new
  `libcurl` dependency, linked only into `ticket-hub-cli`, handles both HTTP delivery and SMTP email.
  Fixed retry policy (10 attempts, 5 minutes apart, then permanently `failed`); a fixed webhook event
  catalog (`ticket.created`/`ticket.status_changed`/`ticket.updated`/`comment.added`); email enqueueing
  mirrors the existing in-app notification set one-for-one, gated on `TICKETHUB_SMTP_HOST` being
  configured. New `GET`/`POST /api/v1/webhooks`, `DELETE /api/v1/webhooks/{id}` routes and a "Webhooks"
  admin screen. Found and fixed a real bug during this batch's own live-delivery verification: a mixed
  anonymous/numbered SQLite placeholder mistake in `recordWebhookDeliveryResult`/
  `recordEmailDeliveryResult` was writing a failed delivery's own id into its `last_error` column instead
  of the actual error message (PostgreSQL's adapter, whose placeholders are always explicit, was
  unaffected) -- see `docs/VERIFICATION.md`'s "Errors and fixes" for the full root cause. New SQLite
  integration and crypto test coverage (including a regression assertion added directly for the bug
  above); verified end-to-end over real HTTP/SMTP against both a fresh SQLite and a fresh live PostgreSQL
  database (real local HTTP/SMTP receivers, independently re-verified HMAC signature and email content)
  and a full Playwright/Chromium browser pass of the new Webhooks screen. Docker daemon unavailable in
  this sandbox, so the `Dockerfile`'s `libcurl` additions were reviewed by inspection only, not
  build-tested.
  **This closes out the entire six-item list the user picked in batch 12 (1, 3, 4, 6, 11, 12).**
- **Post-V1, batch 15.** REST write idempotency keys (D128) -- offered on a follow-up menu after batch 14
  closed the original list; the user picked exactly this one item ("pouze 1"). An optional
  `Idempotency-Key` header, opt-in per request, wired into exactly the five POST routes that create a new,
  independently visible resource (`/api/v1/tickets`, `/api/v1/projects`,
  `/api/v1/tickets/{key}/comments`, `/api/v1/tickets/{key}/worklogs`, `/api/v1/tickets/{key}/clone`) -- a
  retried request with the same key and body replays the original response instead of creating a
  duplicate; the same key reused with a genuinely different body (hashed together with the route path, so
  a cross-route reuse can never be mistaken for a legitimate retry) gets `409`, never a silently wrong
  replay. New `idempotency_keys` table (migration `020_idempotency_keys.sql`,
  `PRIMARY KEY (user_id, idempotency_key)`); `IDatabase::findIdempotencyRecord`/`recordIdempotencyResult`
  on both adapters (the latter best-effort via `INSERT OR IGNORE`/`ON CONFLICT DO NOTHING`, so a narrow
  concurrent-retry race never surfaces as a 500); thin `TicketService` pass-throughs; new `Api.cpp`
  helpers `idempotencyReplay`/`recordIdempotentResult` wired into each route, caching only 2xx responses
  so a corrected retry after a validation failure just runs normally. The demo web UI's five equivalent
  forms/buttons send this header too and now disable their submit control for the duration of the
  request -- complementary defenses, since a fresh key alone can't stop a literal double-click (one is
  minted per handler call) the way disabling the button does, while the key covers the case the button
  can't: a successful response that never reaches the browser, followed by a manual retry. Found and fixed
  a real pre-existing bug while wiring the frontend: `web/app.js`'s shared `api()` helper spread
  `...options` *after* its own merged `headers` object in the `fetch()` call, so any caller passing its
  own `options.headers` (this batch's `idempotencyHeaders()` was the first ever to do so) silently
  clobbered the `Content-Type`/`X-CSRF-Token` headers, causing every wired action to fail with a spurious
  403 -- caught immediately via a live Playwright pass, fixed by reordering the spread. New SQLite
  integration test coverage; verified end-to-end against fresh live PostgreSQL and SQLite databases over
  real HTTP (happy-path replay, conflict, cross-route defense, failed-attempts-never-cached) and a full
  Playwright/Chromium browser pass of all five wired UI actions.
- **Post-V1, batch 16.** Bug fix, user-reported: archiving a project removed it from the active list (D87,
  correct) but there was no substitute view or route to see/unarchive it, and `projectJson()` never even
  serialized `archived`, so the existing per-card Unarchive button was dead code. Added
  `listArchivedProjects` end-to-end (`IDatabase`/`TicketService`/`GET /api/v1/projects/archived`,
  read-access-gated like the active list, not admin-gated like the recycle bin) and an "Archived" toggle in
  `web/`. Verified end-to-end over real HTTP (build/test details in `docs/VERIFICATION.md`).
- **Post-V1, batch 17.** Bug fix, user-reported: every lead/assignee `<select>` in `web/` (Components,
  ticket assignee, bulk assign, the Tickets/Backlog filters, create-ticket) was hardcoded to the same three
  seeded demo accounts instead of the real `state.users` directory, so no other account -- including a real
  admin's own -- could ever be picked. Replaced with one `userSelectOptions()` helper. Frontend-only.
- **Post-V1, batch 18.** Web admin user management (D2/D53/D57), user-requested. Not new scope -- a
  decided-but-never-implemented gap, same class as batch 8. New admin-only "Users" page: list, create
  (web counterpart to `ticket-hub-cli create-user`), deactivate/reactivate, grant/revoke global-admin, and
  an admin-performed temporary-password reset (shown once) -- none of which the CLI could do before this
  batch. No migration (`users.active`/`is_admin` already existed and were already enforced at login). Two
  conservative self-protection defaults with no covering decision text: an admin can never deactivate or
  demote their own account.
- **Post-V1, batch 19.** Kanban board drag-and-drop now reorders a card within its column (D31) instead of
  no-op'ing when dropped back where it started -- user-requested. Reuses the existing
  `POST /api/v1/tickets/{key}/reorder` route the Tickets/Backlog ↑/↓ buttons already call; no new backend
  code, no migration.
- **Post-V1, batch 20.** The two previously queued user requests are complete: Story Points is a fixed
  Jira-style picker (`0`, `0.25`, `0.5`, `1`, `2`, `3`, `5`, `8`, `13`, `20`, `40`, `100`) with a concise
  "relative effort, not hours" explanation on create/edit; the ticket drawer can be resized by drag or
  keyboard, persists its normal width in `localStorage`, and has a full-screen toggle. Frontend-only.
- **Post-V1, batch 21.** Production-readiness and scale-up work (CI/CMake presets, hardened Compose,
  outbox worker/admin UI, backup manifests, FTS5/full-text search, first ES-module split) -- see "Approved
  reliability and scale-up work" above for the full checklist.
- **Post-V1, batch 22.** Build/config fix, user-requested: `TICKETHUB_WEB_ROOT`/`TICKETHUB_MIGRATIONS_ROOT`/
  `TICKETHUB_ATTACHMENTS_DIR` now default relative to the process's current working directory
  (`./web`/`./migrations`/`./data/attachments`) instead of a path compiled into the binary at build time
  (`TICKETHUB_SOURCE_DIR` removed from the `ticket-hub-core` CMake target). No migration, no behavior
  change for Docker/Compose (already sets these variables explicitly).

## Queued next

No work is currently queued. Any new feature needs an explicit product conversation.

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
