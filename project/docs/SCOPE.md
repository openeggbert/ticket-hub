# Scope status

The current build target is the **reduced-scope V1**:
[../REDUCED_SCOPE_SPECIFICATION.md](../REDUCED_SCOPE_SPECIFICATION.md), roadmap in
[REDUCED_SCOPE_ROADMAP.md](REDUCED_SCOPE_ROADMAP.md), and table catalog in
[REDUCED_SCOPE_DATA_MODEL.md](REDUCED_SCOPE_DATA_MODEL.md). The original full-scope
[../SPECIFICATION.md](../SPECIFICATION.md), [ROADMAP.md](ROADMAP.md), and [DATA_MODEL.md](DATA_MODEL.md)
remain as the long-term aspirational baseline only — do not build against them directly.

## Implemented prototype slice

- C++20/CMake project and `TicketHub` namespace.
- Crow route layer (all Phase 1-3 routes, built and live-verified against a real HTTP server — see
  "Server verification" in `README.md` and `docs/VERIFICATION.md`) and a hybrid vanilla HTML/CSS/JS demo
  UI that now covers every one of those routes: login, hierarchy/resolution pickers, full edit/clone/
  links/watch-vote/delete in the ticket drawer, project management, the ticket recycle bin, and reorder/
  move/bulk actions -- all browser-verified with Playwright/Chromium (`docs/VERIFICATION.md`).
- PostgreSQL and SQLite database adapters behind one application-facing interface.
- Ordered checksummed schema migrations and idempotent demo seed.
- Projects and transactional project-local ticket numbering.
- Fixed ticket types/statuses/priorities used by the prototype.
- Ticket creation/list/detail, status changes, labels and comments -- now principal-driven, not the fixed
  demo user (Phase 1).
- Ticket version exposed for optimistic status-change conflict detection.
- Permanent ticket-key alias lookup, now actually written to by `moveTicket` (D37/D38, Phase 3).
- Recycle-bin columns and live-query filtering, now a real recycle bin for both projects and tickets.
- **Local-account identity (Phase 1):** UUID/email users (no `handle` yet), Argon2id password hashing,
  server-side sessions (SHA-256 token hash, 30-day fixed lifetime), minimal login-attempt lockout,
  administrator-only account creation via `ticket-hub-cli create-user`. No self-registration, no
  invitations, no OIDC, no forced-password-change flow -- see `REDUCED_SCOPE_SPECIFICATION.md` section 3.
- **Fixed project-role authorization and project lifecycle (Phase 2):** Viewer/Member/Admin roles plus a
  global-administrator bypass, enforced on every ticket and project write; project create (global admin),
  archive/unarchive (project admin), soft delete/restore/permanent delete (project admin to bin, global
  admin to restore or purge), fixed 90-day on-demand recycle-bin retention; installation-wide anonymous
  read-access toggle, off by default (D59) -- every read use case now takes an optional `Principal`.
- **Fixed hierarchy and workflow rules (Phase 3, complete at the core/CLI/test layer):** the Epic ->
  Story/Task/Bug -> Sub-task hierarchy is enforced on ticket creation (D5, D29, D64-D66); status
  transitions enforce the fixed workflow rules (D68-D70) -- resolution required on completion, cleared on
  reopen, and a parent cannot complete while any sub-task is unfinished; full-replacement ticket edit with
  the same optimistic-locking contract as status changes (D129) -- summary, description, priority,
  assignee, story points, due date, labels, one `ticket_history` row per changed field; the fixed
  ticket-link catalog (D17) -- `blocks`/`relates_to`/`duplicates`/`clones`, visible from both ends,
  requiring project-Member-or-above on both linked tickets' projects; simple field-copy cloning (D60) with
  an automatic `clones` link; self-service watching (D20) and voting (D79), the one ticket write with no
  project-role check; the ticket recycle bin (D22, mirrors the project one) -- soft delete by project
  admin, restore/list/permanent-delete by global admin only; simple bulk actions (D36) --
  status/assignee/label/recycle applied to a list of ticket keys, each through the same single-ticket
  operation, reporting partial success rather than rolling back; simple integer manual ordering with
  full renumbering on every move (D31), replacing the never-used `tickets.rank_value` LexoRank placeholder;
  moving a ticket to a different project (D37) -- no compatibility check needed since every project
  shares the same fixed types/workflow/fields, so a move is a `project_id` change plus a freshly
  allocated key/number, rejected if the ticket has a parent or any children, requiring
  project-Member-or-above on both the source and target projects, with the vacated key becoming a
  permanent alias (D38). See "Not yet built" below for the rest of Phase 3.
- Domain, migration, crypto, identity, SQLite integration, authorization, and workflow tests (7/7
  passing; see `docs/VERIFICATION.md`).
- **Comment editing and tombstone delete (Phase 4, partial, D81/D82/D83):** an `edited_at` timestamp
  instead of a version-history table; soft-delete via the same `deleted_at`/`deleted_by_user_id` columns
  tickets and projects already use, with no separate admin recycle-bin API for comments (the row and
  original body simply remain in the database, excluded from ordinary listing); simplified permissions --
  the comment's own author can always edit/delete it, otherwise the actor needs project-Admin-or-above (or
  global admin), no separate edit-own/edit-all/delete-own/delete-all matrix. Same optimistic-locking
  contract (`expectedVersion` -> `Domain::ConcurrencyConflict`, 409) as ticket edits.
- **Fixed emoji reactions on comments (Phase 4, partial, D84):** a `comment_reactions` many-to-many table
  (mirroring `ticket_watchers`/`ticket_votes`) with a fixed eight-key reaction set (GitHub's own set,
  chosen as a conservative default since the decision register does not enumerate one); self-service,
  no project-role check, same reasoning as watch/vote (D20/D79); idempotent add/remove.
- **@mention handles and the fixed in-app notification set (Phase 4, partial, D56/D80/D14):** an optional,
  unique `users.handle` (admin-set at account creation, no self-service profile editing yet); comments
  are scanned once, at creation, for `@handle` tokens, notifying each resolved user; three fixed
  notification types only -- assigned, mentioned, comment on a watched ticket -- no email, no
  admin-configurable schemes, no per-user preferences/digests; a recipient who is both mentioned and
  watching the same comment gets one notification, not two. `GET /api/v1/users` backs @mention autocomplete
  in the comment textarea; a notification bell with an unread badge in the UI opens a panel that marks
  notifications read on click and navigates to the related ticket.
- **Rendered Markdown, visual toolbar, and live preview (Phase 4, D16):** comment bodies and ticket
  descriptions render as formatted HTML (bold/italic/inline code/links/headings/lists/blockquotes/fenced
  code/rules) via a deliberately small, safe-by-construction subset -- input is HTML-escaped first, then
  wrapped in a fixed set of hardcoded tags, so there is no separate sanitization pass to get wrong.
  Bold/Italic/Code/Link/list/Quote toolbar buttons plus a live-preview toggle on every Markdown-capable
  textarea.
- **Simplified worklogs (Phase 4, D12/D13):** time spent + an optional comment only, no remaining-estimate
  linkage (D12 dropped time estimates from V1 entirely); no own-vs-others edit/delete permission split
  (D13) -- any project member with ticket access may edit or delete any worklog on that ticket, not just
  the one they logged themselves, unlike comments' author-or-admin rule (D83). Tombstone delete and the
  same optimistic-locking contract as comment/ticket edits.
- **Simple append-only admin/security audit log (Phase 4, D23):** an `audit_events` table with no
  categories-as-a-retention-feature, export, or configurable retention -- rows are never purged.
  Recorded automatically for a small, focused set of admin/security-relevant writes (failed/blocked
  login, user creation, the anonymous-read toggle, permanently deleting a project or ticket), not as a
  general-purpose hook on every write. Global-administrator-only read access, via a new "Audit log" nav
  item in `web/`. **This closes out Phase 4 (Collaboration) -- every item in
  `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 4 list is now implemented.**
- **Ad-hoc ticket filter/search widening (Phase 5, D10/D43):** `GET /api/v1/tickets` and `Domain::TicketFilter`
  now also support type/priority/assignee/label/due-date filters (in addition to project/status/search),
  and search now also matches ticket description, not just summary/key. Still ad-hoc, in-UI-only filters
  (no saved/shared filters, no JQL, not usable as a webhook/board source) and still a plain `LIKE`/`ILIKE`
  substring match (no full-text index). This is the first Phase 5 slice.
- **Personal dashboard widgets (Phase 5, D24):** `GET /api/v1/dashboard` now returns `assignedToMe`,
  `watchedTickets`, and `upcomingDeadlines` for an authenticated caller (empty for anonymous). Matches
  D24's fixed widget set (assigned tickets, watched tickets, recent activity, deadlines, simple stats) minus
  the active-sprint widget, dropped since Scrum was removed for V1. `assignedToMe`/`upcomingDeadlines`
  exclude Done-category tickets.
- **Kanban board WIP limits (Phase 5, D32/D33):** `GET /api/v1/board-columns` and
  `PUT /api/v1/board-columns/{statusKey}` (global-administrator-only). A single flat, installation-wide
  table -- one row per fixed workflow status, no per-project scoping at all, following
  `docs/REDUCED_SCOPE_DATA_MODEL.md`'s target schema literally. Soft, display-time-only: an over-limit
  column is highlighted in `web/`'s Board view, never blocked from receiving more tickets.
- **Attachments (Phase 5, D15/D98-D105):** the full vertical -- upload, download, delete, and a recycle
  bin, all via `/api/v1/tickets/{key}/attachments` plus `/api/v1/attachments/{id}/...`. Local filesystem storage
  only, hardwired (D15, no S3/pluggable backend). Fixed limits (D98): 25MB/file, 20/ticket, a blocked-
  extension denylist, no admin configuration. All four native-element previews (D99): image, PDF, text,
  audio/video. Full upload + drag/drop + paste in the Markdown editor (D100), referencing
  `attachment://<id>`. Sortable list + recycle bin (D101), fixed 90-day on-demand retention (D102).
  Upload-time SHA-256/size verification only, no periodic integrity audit (D105). **This closes out Phase
  5 -- every item in `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 5 list is now implemented, and Milestone 2 is
  fully closed.**
- **Personal access tokens (Phase 6, D39/D40):** self-service `GET`/`POST /api/v1/tokens`,
  `DELETE /api/v1/tokens/{id}`. Hashed storage, mandatory expiration, revocation, last-used tracking; no
  scopes/rotation/admin-configurable lifetime. `Authorization: Bearer <token>` now authenticates any
  route (mutually exclusive with the session cookie per request, D54); Bearer-authenticated requests are
  exempt from the CSRF check.
- **Active-session list and "sign out everywhere" (Phase 6, D54):** `GET /api/v1/sessions`,
  `POST /api/v1/sessions/sign-out-others` (session-cookie-only, not usable via PAT). Keeps the caller's own
  current session active -- a conservative default, no decision text specifies this.
- **Account settings web UI (post-V1):** a new "Account" page in `web/` (create/list/revoke personal
  access tokens, list/sign-out active sessions), the first optional follow-up item picked after V1 closed.
  No backend or schema changes -- consumes the endpoints above, which already existed.
- **Fixed rate limits (Phase 6, D124/D125):** a new in-memory `TicketHub::Web::RateLimiter` (fixed-window,
  no admin config) caps `/api/v1/auth/login` at 20 attempts per IP per 15 minutes (on top of, not instead of,
  the existing per-account 10-attempts/15-minutes lockout) and every write route at 120 requests per
  minute (keyed by user id when authenticated, else by IP), both returning 429 with a `Retry-After` header
  on trip. Process-lifetime in-memory state only; resets on restart.
- **Versioned `/api/v1` prefix (Phase 6, D127):** every route now lives under `/api/v1` except
  `GET /api/health`, deliberately kept unversioned (common infra/monitoring convention; not specified by
  any decision text, documented here explicitly). Formal `/api/v2` deprecation policy deferred until a
  real v2 is needed, per D127.
- **Read-only CSV export of tickets (Phase 6, D48):** `GET /api/v1/tickets/export.csv`, sharing
  `Domain::TicketFilter`'s query-parameter filters and authorization with `GET /api/v1/tickets`. No CSV
  import, no Jira migration tool. `web/`'s Tickets view gained an "Export CSV" link matching the current
  filters.
- **Fixed request-body/bulk-item constants (Phase 6, D125):** every JSON request body capped at 1 MiB
  (`413` if exceeded); every bulk-action `ticketKeys` array capped at 200 items (`400` if exceeded). No
  admin configuration.
- **Security hardening pass (Phase 6):** found and fixed a real stored-XSS vulnerability -- an uploaded
  attachment's `Content-Type` is caller-supplied and unvalidated (D98 has no upload-time MIME allow-list),
  and a spoofed `text/html` value could execute an embedded `<script>` via direct download-URL navigation
  (`Content-Disposition: inline`) or the app's own text/PDF preview (an unsandboxed `<iframe>`). Fixed:
  both preview iframes now carry `sandbox=""`; the download route now serves `Content-Disposition:
  attachment` for any content type on a new document/script-capable deny-list (html/xhtml/svg/xml/
  javascript variants). Also added standard security headers (`X-Content-Type-Options`, `X-Frame-Options`,
  `Referrer-Policy` on every response; a `Content-Security-Policy` with `script-src 'self'` on the HTML
  document). Dependency review (Crow pinned to a release tag) and session/CSRF cookie review both found
  no other tickets.
- **Numbered/offset pagination (Phase 6, D126):** `GET /api/v1/tickets` accepts `page`/`pageSize` (default/
  max 200, giving D125's "max page size" its concrete value); response gains `page`/`pageSize`/
  `totalItems`/`totalPages`. Fixed, along the way, a previously-undocumented bug: the route's SQL had
  always silently capped results at 200 rows with no `total` returned. A deliberate partial rollout --
  every other list endpoint remains unpaginated, documented as still open. **This closes Phase 6.**
- **Backup and restore (Phase 7, D106-D108):** `ticket-hub-cli backup <output-directory>` (copies the
  attachments directory, dumps the database -- SQLite online backup API; PostgreSQL `pg_dump --clean
  --if-exists`) and `ticket-hub-cli restore <backup-directory> --yes` (mandatory confirmation flag;
  restores database + attachments, then runs pending migrations). Offline/maintenance-window use only, no
  isolated staging environment. D111 (upgrades) needed no new work -- `ticket-hub-cli migrate` already
  satisfies it. Live-verified end-to-end on both SQLite and PostgreSQL matching the exit gate exactly.
- **Structured JSON logs (Phase 7, D133):** a new `TicketHub::Web::JsonLogHandler` replaces Crow's
  default stderr text logger, so every log line (startup, per-request, warnings/errors) is one JSON
  object per line on stdout. No Prometheus, no OpenTelemetry.
- **Admin version banner (Phase 7, D112):** `GET`/`PUT /api/v1/settings/latest-known-version`
  (global-administrator-only, reuses the existing generic `installation_settings` key/value table, no new
  migration) plus a `web/` banner shown to admins when the admin-configured "latest known version"
  differs from the running one. No automatic update-check mechanism -- no decision text specifies one and
  no outbound-HTTP-client infrastructure exists in this codebase; an admin sets the value manually.
  **This closes Phase 7's entire roadmap list, and with it, Milestone 3.**
- **Docker image and Compose distribution (Phase 8, D50):** a two-stage `Dockerfile` (compile with the
  full toolchain, run on a slim image with only the required shared libraries) and a `ticket-hub` service
  in `docker-compose.yml` alongside `postgres`, so `docker compose up` alone brings up the full instance.
  The only supported distribution path for V1 -- no `.deb`/`.rpm`, no Helm/Kubernetes. Verified as far as
  this environment's network policy allows (`docker build --check`/`docker compose config` pass cleanly;
  the runtime configuration was verified directly on the host against both SQLite and live PostgreSQL);
  the actual image build was blocked here by a network-egress policy denial, precisely disclosed in
  `docs/VERIFICATION.md`, not worked around.
- **Light and dark theme (Phase 8, D46):** `web/styles.css` follows the OS-level `prefers-color-scheme`
  signal automatically via `color-scheme: light dark` and a full `@media (prefers-color-scheme: dark)`
  variable-override block -- no manual in-app toggle or persisted preference, since D46 only calls for
  "light and dark theme... simple". ~30 hardcoded literal colors converted to the CSS custom-property set
  so the whole UI themes consistently. Browser-verified with Playwright/Chromium in both modes; found and
  fixed one real bug (`.link-form input` had no dark styling and rendered as a stray white box).
- **Accessibility baseline pass and browser-support note (Phase 8, D47/D139):** found and fixed one real
  keyboard-operability gap -- ticket table rows, Kanban board cards, project cards, and inline ticket-key
  cross-reference links were mouse-click-only, unreachable by keyboard. Fixed with a shared
  `makeKeyboardActivatable` helper (`tabindex="0"`, `role="link"`, `Enter`/`Space` handling) plus a visible
  focus-ring CSS rule. No formal WCAG audit -- not required by D47's reduced V1 scope. D139 (browser
  support) reconfirmed satisfied by construction, no code change. Browser-verified with Playwright/
  Chromium (keyboard-only activation of each fixed element, no mouse-click regression) plus a clean
  regression re-run.
- **Threat-model / security self-review (Phase 8):** full write-up in `docs/THREAT_MODEL.md`. Found and
  fixed a real broken-access-control (IDOR) bug -- `editComment`/`deleteComment`/`editWorklog`/
  `deleteWorklog`/`deleteAttachment` checked the caller's project role against the URL's ticket but looked
  up the target resource purely by id, letting a user with a role on one project reach a comment/worklog/
  attachment belonging to a different project by routing through their own ticket's URL. Also fixed: a
  CSRF cookie that was an unnecessary literal prefix of the session token, a login timing side channel
  enabling email enumeration, a missing CSRF check on `/api/v1/auth/logout`, and CSV formula injection in
  the export route. New regression tests cover the IDOR fix; every fix reproduced and confirmed live over
  HTTP. **This closes Phase 8, Milestone 4, and the entire reduced-scope V1 roadmap.**

## Post-V1 (optional, non-roadmap follow-up, picked one at a time by explicit user choice)

- **Batch 1 (done): account settings web UI** -- personal access tokens and active sessions management,
  both already had a complete REST API since Phase 6, only the `web/` surface was missing.
- **Batch 2 (done): re-typing and re-parenting a ticket after creation** -- the one item left open since
  Phase 3. `editTicket` now edits `ticketTypeKey`/`parentTicketKey` too, re-validating the fixed hierarchy
  shape and rejecting a hierarchy-level retype while the ticket has children (checked transactionally,
  same precedent as `moveTicket`'s own "has children" rule). See `docs/VERIFICATION.md` for detail.
- **Batch 3 (done): Kanban board drag-and-drop** -- cards can now be dragged directly between columns
  instead of only via the drawer's status dropdown, which remains the keyboard-operable path (native HTML5
  drag-and-drop has none). A Done-category drop without an existing resolution opens a small prompt first,
  same D68-D70 rule as the dropdown. No backend/schema/API changes. See `docs/VERIFICATION.md` for detail.
- **Batch 4 (done): bulk Done-status picker and keyboard multi-select** -- the bulk status picker now
  includes Done-category statuses with a shared-resolution prompt (the backend already supported this; only
  the UI was missing); the tickets table gained a header "select all" checkbox, Shift+click range select,
  and Shift+ArrowDown/ArrowUp keyboard range extension. Both `web/`-only. See `docs/VERIFICATION.md` for
  detail.
- **No further items remain on the original post-V1 follow-up list** -- every one is done.
- **Batch 5 (done): Jira-style `/browse/{key}` direct ticket links** -- a new user-requested addition after
  the original list closed. New `GET /browse/<key>` server route plus `history.pushState`/`popstate` URL
  syncing in `web/app.js`. No backend/schema changes beyond the route. See `docs/VERIFICATION.md`.
- **Batch 6 (done): full "issue" -> "ticket" terminology rename** -- database schema, REST API paths and
  JSON fields, C++ code, and all UI text renamed to match the product's own name, migration
  `016_ticket_terminology.sql`. Breaking API change (`/api/v1/issues` -> `/api/v1/tickets`); no compatibility
  shim, since V1 has no external API consumers yet. See `docs/VERIFICATION.md`.
- **Batch 7 (done): project components (D19, `KEEP_FOR_V1`)** -- the one V1-decided feature that was never
  actually implemented, discovered while answering a user question about what entity fields tickets
  support. Simple project components: name, description, lead, default assignee, at most one per ticket
  (migration `017_project_components.sql`), exactly as originally decided -- no scope reduction needed,
  since D19 was already "the cheapest reasonable form." New `GET`/`POST /api/v1/projects/{key}/components`
  and `PATCH`/`DELETE /api/v1/projects/{key}/components/{id}` routes (project-Admin-or-above to
  create/edit/delete, same read access as projects/tickets to list); `createTicket`/`editTicket` gained a
  `componentName` field and `TicketFilter` gained a matching `componentName` filter; `cloneTicket` now
  copies the component too, per the original clone decision text. No recycle bin -- D19 doesn't call for
  one; deleting a component clears it from any ticket via `ON DELETE SET NULL`. `web/` gained a
  project-card "Components" management dialog and a Component picker/filter/display in the ticket
  create/edit/drawer/table surfaces. See `docs/VERIFICATION.md`.
- **Batch 8 (done): five V1-decided features that were never actually implemented**, found by re-auditing
  every `KEEP_FOR_V1`/`ALREADY_IMPLEMENTED_AND_KEEP` decision in `docs/REDUCED_SCOPE_DECISIONS.md` against
  the real codebase:
  - **D62 (Markdown checklist syntax)**: `- [ ] foo` / `- [x] bar` list items now render as real, disabled
    `<input type="checkbox">` elements (checked state preserved) instead of literal bracket text, in
    `renderMarkdown` (`web/app.js`). No backend change -- Markdown rendering is entirely client-side.
  - **D66 ("No Epic" ticket filter)**: a client-side-only `#ticket-epic-filter` on the Tickets view (`Any
    hierarchy` / `No Epic`), matching D10's "ad-hoc, in-UI-only filters" convention -- filters the
    already-fetched ticket list to top-level tickets with no parent, no new backend field/query-param.
  - **D129 (stale-write conflict dialog)**: an HTTP 409 from a ticket edit (optimistic-lock version
    mismatch) now surfaces a `showConflictDialog` modal ("Someone else changed this ticket... Reload
    latest version") instead of a generic error toast; reloading re-opens the ticket in edit mode with
    fresh server data. `api()` now attaches `error.status` to thrown errors so call sites can branch on it.
  - **D45 (per-user timezone/clock-format preferences)**: `time_zone`/`clock_format` on `users` (already
    present in the schema) are now self-service via `PATCH /api/v1/account/preferences`
    (`IDatabase::updateUserPreferences`, `Domain::validateUpdatePreferences`), exposed as a new
    "Preferences" panel on the Account view with a "Detect from browser" button
    (`Intl.DateTimeFormat().resolvedOptions().timeZone`). `Domain::Principal` gained `timeZone`/
    `clockFormat` fields (defaulted so every pre-existing 4-argument brace-init call site still compiles).
    The web client tracks "has this browser's user manually set this" in `localStorage` (not a server
    column) so auto-detect only fires once per browser and never clobbers a deliberate choice (including a
    deliberate UTC). `formatDate` now sniffs date-only values (`YYYY-MM-DD`, e.g. due dates, worklog dates)
    and renders them in UTC with no time-of-day and no timezone shift, per D45's own text; timestamps
    render in the viewer's stored timezone/clock format.
  - **D91 (changing an active project's key)**: `PATCH /api/v1/projects/{key}/key` (project-Admin-or-above,
    `IDatabase::changeProjectKey`) renames a project's key inside one transaction: the vacated key becomes
    a permanent `project_key_aliases` row (previously a dead/unused table, now populated), and every ticket
    in the project -- including soft-deleted ones -- is renamed to the new prefix with the same numeric
    suffix, its own vacated key becoming a `ticket_key_aliases` row exactly like `moveTicket` (D38) does for
    a single ticket. Rejects a `newKey` already live or already reserved by another project's alias. New
    "Rename key" button/modal on each project card in `web/app.js`. Two bugs were caught and fixed during
    browser verification of this batch, not scoped to D91 alone: (1) the client's `state.selectedProject`
    was not updated when the currently-selected project was renamed, silently emptying the Tickets/Board
    views until a manual reselect; (2) `.modal-backdrop` had the same `z-index` as `.drawer-backdrop` (80),
    lower than `.ticket-drawer` (90), so any modal opened while the ticket drawer is showing -- most
    importantly D129's conflict dialog, triggered by a failed save from inside the drawer's edit form --
    rendered behind the drawer and was unclickable; `.modal-backdrop` now has its own `z-index: 100`.

  New tests: `tests/domain_validation_tests.cpp` (`validateUpdatePreferences`/`isValidClockFormat`),
  `tests/sqlite_integration_tests.cpp` (`updateUserPreferences`/`findUserById` round-trip;
  `changeProjectKey` success, alias creation, ticket bulk-rename, both collision cases, unknown-key
  no-op), `tests/authorization_integration_tests.cpp` (`changeProjectKey` project-Admin-or-above gate,
  self-rename/collision/unknown-key rejections). All three build configurations (default, SQLite-only,
  PostgreSQL-only) compile clean; live-verified against a fresh PostgreSQL database and a fresh SQLite
  database via `curl` (preferences round-trip, project/ticket key rename and alias resolution, 409
  conflict response); browser-verified end-to-end with Playwright/Chromium against a fresh SQLite database
  (all 5 behaviors, 13/13 checks). See `docs/VERIFICATION.md`.
- **Batch 9 (done): a dedicated Backlog screen, Markdown-supported worklogs, and Created/Updated columns**
  -- three user-requested changes in one pass:
  - **Backlog gets its own screen, amending D32.** D32 ("one board column equals one workflow status")
    originally put Backlog on the board like every other status; the user pointed out a project's backlog
    can grow far past what a Kanban column usefully displays, so Backlog is removed from the board (now 4
    columns: Confirmed, In Progress, In Review, Done) and gets a new dedicated, paginated "Backlog" screen
    instead (`web/app.js` `renderBacklog`, new nav item between Board and Tickets). Unlike every other list
    in this app -- which all still rely on the fixed 200-row cap -- this screen uses real server-side
    pagination (`page`/`pageSize`, the existing D126 contract) since backlog size is explicitly unbounded
    by design. Ordered by a new `sort=rank` query parameter (`Domain::TicketFilter::sortByRank`, both
    adapters' `listTickets` overloads switch their `ORDER BY` from `updated_at DESC` to `rank_order,
    ticket_number` when set) so "page 1" is always the top of the backlog by priority, not an arbitrary
    recency-based cut -- the manual reorder arrows (D31) work exactly as they already do on the Tickets
    view, just always enabled here since the screen is always scoped to one project. The Board's own
    ticket fetch changed too: instead of one unfiltered `fetchTickets()` call, `fetchBoardTickets()` issues
    one request per board status, so the board's fixed page-size budget is spent entirely on statuses it
    actually renders instead of possibly being consumed by backlog rows that would never appear on it
    anyway (a real, if narrower-than-advertised, correctness problem the old single-fetch approach had
    once a project's backlog grew past ~200 tickets). "Board"/"Backlog" shortcut buttons link the two
    screens both ways.
  - **Worklogs are Markdown-supported, not forced single-line.** The "what did you work on" field was a
    plain single-line `<input>`, rendered with `escapeHtml` into inline text -- the backend already allowed
    up to 10,000 characters with no single-line restriction (`Domain::validateAddWorklog`), so this was a
    pure frontend gap. Changed to a `<textarea>` with the exact same Markdown toolbar (`attachMarkdownToolbar`)
    and @mention autocomplete already used for comments and the description field, rendered through
    `renderMarkdown` into a `.markdown-body` block instead of an inline escaped span.
  - **Created/Updated columns on every ticket table.** The ticket detail drawer already showed both (`ticket.
    createdAt` via `formatDate`, `ticket.updatedAt` via `relativeDate`) but neither ticket *list* did.
    `ticketRows` (shared by the Tickets view and the new Backlog screen) gained both columns, behind a new
    `showTimestamps` option defaulting to `true`; the Dashboard's compact "Assigned to me"/"Watching"/
    "Recently active" widgets (`tablePanel`) explicitly opt out (`showTimestamps: false`) to stay terse,
    matching their own "focused overview" framing.

  No backend schema change -- `sort=rank` is a new, backward-compatible query parameter (existing callers
  that never send it get the exact same `updated_at DESC` order they always did); `created_at`/`updated_at`
  were already returned by every ticket JSON response, just not rendered in list views. New test coverage:
  `tests/sqlite_integration_tests.cpp` asserts `sortByRank` orders both the unpaginated and paginated
  `listTickets` overloads by `rank_order`/`ticket_number`, and that omitting it keeps the pre-existing
  `updated_at`-based order. Verified: full rebuild and `ctest` clean in all three build configurations; a
  fresh live PostgreSQL database exercised over HTTP (`status=selected` on the board-status endpoint
  excludes backlog tickets; `sort=rank` paginated backlog listing returns the lowest-rank ticket first; a
  multi-line, Markdown-syntax worklog comment round-trips byte-for-byte); a full Playwright/Chromium
  browser pass against a fresh SQLite database seeded with 60+ backlog tickets (19/19 checks: no Backlog
  column on the board, Backlog/Board shortcut buttons both directions, correct page-1/page-2 row counts and
  Previous/Next button disabled states, reorder arrows changing row order, Created/Updated columns present
  on both the Tickets and Backlog tables and absent on the Dashboard's compact widgets, the worklog field
  being a textarea with a Markdown toolbar, and a submitted worklog rendering **bold** text and a bullet
  list as real HTML rather than literal Markdown syntax). See `docs/VERIFICATION.md`.
- **Batch 10 (done): ticket detail drawer restyled to look more like Jira** -- a pure UI/layout change
  (no API/schema changes) requested directly by the user ("prosim at je layout detailu ticketu vice
  podobny jire" -- "please make the ticket detail layout more similar to Jira"). The status select moved
  out of the sidebar into a prominent, colored pill-button (`.status-pill`, reusing the existing todo/
  in_progress/done category colors from `.status-chip`) right under the title, with the resolution
  picker/confirm flow inline next to it instead of buried in a `meta-row`. The sidebar became two
  bordered "Details" and "Dates" panel cards (Assignee/Reporter/Priority/Labels/Component/Story points/
  Due date/Parent/Move-to-project, then Created/Updated separately, mirroring Jira's own panel split).
  Comments and Work log became tabs in a single "Activity" section (`.activity-tabs`, one panel each,
  toggled by a new `activeActivityTab` module-level variable so the choice survives a full drawer
  re-fetch -- e.g. logging time keeps the Work log tab active so the new entry is visible immediately
  without an extra click) instead of two always-visible stacked sections; each panel's add-form now sits
  above its list, matching Jira's comment-box-at-top convention. "Links" was relabeled "Linked issues".
  Every existing interactive behavior (watch/vote/clone/edit/delete, status change, resolution confirm/
  cancel, links, attachments, comments including edit/delete/reactions, worklogs, move-to-project) kept
  its exact same element `id`s/`data-*` attributes and event wiring -- only the surrounding markup and
  CSS moved, so no JavaScript logic needed to change for any of them, only the new tab-switching handler.
  Two real bugs were found and fixed during this batch's own browser verification: (1) a pre-existing
  backend bug in `changeTicketStatus` (both adapters) -- confirming a resolution while the requested
  status equals the ticket's current status (e.g. a ticket that is already Done-category but has no
  resolution recorded, which can happen with historical/imported data) hit a same-status no-op guard that
  silently discarded the resolution and the whole request, even though the UI's resolution-confirm flow
  is legitimately reachable in exactly that state; fixed by narrowing the no-op guard to skip only when
  there is truly nothing to change, so a same-status call is still applied when it's newly supplying a
  resolution that was missing. This bug pre-dates this batch (the interactive resolution flow existed
  identically in the old sidebar-buried layout) but became more likely to be hit once the redesign made
  it more prominent, so it was fixed as part of this batch rather than left in a newly-showcased control.
  (2) A CSS regression introduced by this batch itself: `.resolution-inline { display: flex; }` overrides
  the browser's default `[hidden] { display: none }` rule (same selector specificity, later in the
  cascade), so the resolution picker was showing even for non-Done statuses until a `.resolution-inline
  [hidden] { display: none; }` override was added.
  New test coverage: `tests/sqlite_integration_tests.cpp` covers the resolution no-op-guard fix directly
  (forces a ticket into Done with no resolution via raw SQL, since the normal API can't produce that
  state; confirms a same-status call with a resolution now persists it and bumps the version; confirms a
  further same-status call once a resolution already exists remains a true no-op, neither overwriting the
  resolution nor bumping the version). Verified: full rebuild and `ctest` clean in all three build
  configurations; the resolution-confirm fix live-verified over real HTTP against both a fresh PostgreSQL
  database and a fresh SQLite database; a full Playwright/Chromium browser pass (20/20 checks) covering
  the pill/toolbar/sidebar-panel structure, both activity tabs (including that logging time keeps the
  Work log tab active and the new entry's Markdown renders), and every pre-existing action (watch/edit/
  status-change/resolution-confirm) still working through the restyled markup; new screenshots for all
  three status-category pill colors and dark mode confirmed the `[hidden]` CSS fix. README's ticket-detail
  screenshot regenerated. See `docs/VERIFICATION.md`.
- **Batch 11 (done): a History activity tab on the ticket detail drawer** -- requested directly by the
  user right after Batch 10 landed ("a co pridat i zalozky history activity transitions ???" -- "and what
  about also adding History/Activity/Transitions tabs?"). `ticket_history` (`id`, `ticket_id`,
  `actor_user_id` nullable, `field_name`, `old_value`, `new_value`, `created_at`) already existed and was
  already written to by `changeTicketStatus`/`editTicket`/`moveTicket`, but had no read-side API --
  purely write-only internal bookkeeping until now. Added `IDatabase::listTicketHistory(ticketKey)` to
  both adapters (newest first, `LEFT JOIN users` since `actor_user_id` is nullable unlike
  `comments`/`worklogs`' `author_user_id`), `TicketService::listTicketHistory` (read-access-gated, same
  shape as `listComments`/`listWorklogs`), and a new `GET /api/v1/tickets/{key}/history` route
  (`ticketHistoryEntryJson` serializer in `src/web/Api.cpp`). The drawer's Activity section gained a third
  tab, "History (N)", next to Comments and Work log, rendering each row as a Jira-style sentence ("Demo
  Admin changed priority from "Highest" to "Medium" / 2 minutes ago") via new frontend-only formatting
  helpers (`historyChangeText`/`historyValueLabel`/`historyFieldLabel` plus small `PRIORITY_LABELS`/
  `TICKET_TYPE_LABELS`/`HISTORY_FIELD_LABELS` lookup maps in `web/app.js`) that turn the raw stored keys
  (a status/priority/type key, a snake_case field name) into the same display labels used everywhere else
  in the UI, without changing what the API stores or returns. No schema or migration change -- this batch
  is additive read-exposure of an existing table only.

  New test coverage: `tests/sqlite_integration_tests.cpp` asserts `listTicketHistory` returns the
  status-change row plus one row per field actually changed by a full edit, newest-first ordering (a
  strict `>` comparator on `createdAt`, since several rows from one `editTicket` transaction share the
  exact same timestamp and a non-strict `>=` comparator is not a valid strict weak ordering for
  `std::is_sorted`), correct old/new values on the status entry, actor resolution through the nullable FK,
  and an empty result for an unknown ticket key rather than an error. Verified: full rebuild and `ctest`
  clean in all three build configurations; live-verified over real HTTP against both a fresh PostgreSQL
  database and a fresh SQLite database (a status transition and a full edit each produce the expected
  history rows with correctly resolved actor and old/new values; a genuine same-status no-op, confirmed by
  checking the ticket's pre-existing status first, correctly produces no new row); a full Playwright/
  Chromium browser pass (`verify_history_tab.js`, 8/8 checks) covering the three-tab layout, the History
  panel being hidden by default and visible after its tab is clicked, a real edit producing readable
  "changed summary"/"changed priority" entries with humanized labels (not raw snake_case field names or
  raw priority/status keys), and a status transition producing a readable status-name entry. README's
  ticket-detail screenshot regenerated to show the History tab active. See `docs/VERIFICATION.md`.

## Not yet built (still V1 scope — see `REDUCED_SCOPE_ROADMAP.md`)

- Phases 1-8 (the entire reduced-scope V1 roadmap) are complete -- nothing remains in this category.
  Everything below this point is either post-V1 optional follow-up (above) or permanently out of scope.
- **Milestone 3 (Phases 6 and 7) is fully complete.** Pagination (D126) covers `GET /api/v1/tickets` only
  so far -- every other list endpoint (projects, comments, worklogs, attachments, notifications,
  sessions, tokens, audit events, watchers, voters, board-columns, ticket-links, comment-reactions)
  remains unpaginated; extending it further is optional follow-up, not a blocker to Phase 6's exit gate.
- **Phase 8 (Milestone 4) is fully complete**, which closes the entire reduced-scope V1 roadmap: Docker
  packaging (D50), light/dark theme (D46), the accessibility baseline pass (D47), the browser-support note
  (D139), and the threat-model/security self-review (`docs/THREAT_MODEL.md`) are all done.

## Permanently out of V1 scope (do not build these)

OIDC, invitations, public registration, configurable permission/notification schemes, the configurable
workflow engine, custom fields, saved/shared filters, Scrum/sprints/agile reports, versions/releases,
ticket templates, automation rules, webhooks, service accounts, Git integration, the Jira migration
tool, CSV import, outbound/inbound email, the background job queue, internal event bus, realtime
(SSE), in-memory cache, S3 attachment storage, Kubernetes/Helm/`.deb`/`.rpm` packaging, the i18n
framework, and pluggable secrets/observability backends. Full list with decision numbers:
[REMOVED_AND_DEFERRED_FEATURES.md](REMOVED_AND_DEFERRED_FEATURES.md).

Do not treat the current UI or `IDatabase` shape as the final API. They are a working vertical slice
used to evolve the architecture incrementally.
