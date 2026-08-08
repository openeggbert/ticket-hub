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
  (no saved/shared filters, no JQL, not usable as a webhook/board source); migration 021 replaces the
  former `LIKE`/`ILIKE` substring scan with native SQLite FTS5 and PostgreSQL full-text indexes.
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
- **Backup and restore (Phase 7, D106-D108; strengthened post-V1):** `ticket-hub-cli backup
  <output-directory>` copies attachments, writes the database dump (SQLite online backup API; PostgreSQL
  `pg_dump --clean --if-exists`), and writes a version/migration-catalog/file-checksum manifest.
  `ticket-hub-cli verify-backup <backup-directory>` validates it without writes. Restore now requires
  `restore <backup-directory> --yes --maintenance`, validates the manifest before changing data, replaces
  the attachment directory exactly, then runs pending migrations. It remains an offline maintenance-window
  operation with no isolated staging environment. D111 (upgrades) remains `ticket-hub-cli migrate`.
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
- **Batch 12 (done): quick filters, more keyboard shortcuts, and pagination extended to notifications and
  the audit log** -- three of six items the user picked off a menu of possible new functionality asked for
  directly ("napis mi seznam moznych novych funkcionalit a ja se rozhodnu" -- "write me a list of possible
  new functionalities and I'll decide"); the other three (custom fields, webhooks, outbound email) are
  larger, decision-register-deferred features tracked separately (see the "Deferred after V1, in
  progress" note below).
  - **Quick filters (Board/Backlog).** Two one-click chip toggles -- "Only my tickets" and "No Epic" --
    reusing the exact same semantics as the Tickets screen's existing assignee dropdown and D66's "No
    Epic" filter, applied client-side after the normal fetch so the same `applyQuickFilters`/
    `quickFiltersBar` pair covers both screens regardless of whether the underlying fetch is paginated
    (Backlog) or not (Board). Deliberately separate state fields (`quickFilterMine`, `quickFilterNoEpic`)
    from the Tickets screen's own filter-bar state, since Board already resets every filter-bar field on
    each render and mixing the two would either break that reset or leak a Tickets-screen filter onto
    Board unexpectedly.
  - **More keyboard shortcuts.** `/` focuses the global search box; `?` opens a new "Keyboard shortcuts"
    help modal (also reachable via a topbar button); `c` (pre-existing) still opens the create-ticket
    modal. Table rows and board cards were already focusable and Enter-activatable
    (`makeKeyboardActivatable`, from the D47 accessibility pass) but only reachable one Tab press at a
    time -- `bindTicketLinks` now also wires Up/Down to move focus directly to the previous/next
    ticket in reading order, and a new `bindBoardKeyboardNav` wires Left/Right on the board specifically
    to move to the same row position in the adjacent column (plain "next element in DOM order," the rule
    Up/Down uses, would just walk down the current column instead, since board cards are grouped by
    column in the markup).
  - **Pagination extended to notifications and the audit log** (D126 extension). Scoped down from the
    original four-endpoint ask (comments/worklogs/notifications/audit log) to just these two: a per-ticket
    comment or worklog list is naturally bounded the same way `ticket_history` already was reasoned to be
    (see Batch 11's rationale) and doesn't need paging, but a per-user notification list and the
    installation-wide, append-only-forever audit log both have the same unbounded-growth shape that
    justified D126 for tickets in the first place. New `IDatabase::listNotifications(userId, unreadOnly,
    limit, offset)` / `countNotifications`, `IDatabase::listAuditEvents(limit, offset)` / `countAuditEvents`
    on both adapters, `TicketService::listNotificationsPaged`/`listAuditEventsPaged`, and both existing
    routes (`GET /api/v1/notifications`, `GET /api/v1/admin/audit-events`) now accept the same optional
    `page`/`pageSize` query parameters as `GET /api/v1/tickets`, wrapped in the same
    `items`/`page`/`pageSize`/`totalItems`/`totalPages` envelope -- both routes already returned an
    `{items: [...]}` shape, so this is purely additive for any existing caller that only reads `.items`.
    The admin Audit log screen gained Previous/Next pagination controls (mirroring the Backlog screen); the
    notification bell panel is left as-is (a capped, most-recent-200 read, matching how every other
    non-Backlog ticket list in this app already behaves).

  New test coverage: `tests/sqlite_integration_tests.cpp` asserts `countNotifications`/`countAuditEvents`
  agree with their unpaginated counterparts' row counts, that the paginated overloads honor
  limit/offset without overlapping or reordering pages, and that an offset past the end returns an empty
  page rather than an error. Verified: full rebuild and `ctest` clean in all three build configurations;
  live-verified over real HTTP against a fresh PostgreSQL database (notification pagination exercised with
  a second real user receiving three `assigned` notifications from ticket creation, confirming correct
  paging and that the `unread` filter composes correctly with `page`/`pageSize`; audit-log pagination
  exercised against the events the verification session's own user-creation calls produced) and a full
  Playwright/Chromium browser pass against a fresh SQLite database (11/11 checks: the shortcuts modal
  opens/closes on `?`/Escape, `/` focuses the global search box, both quick-filter chips render and toggle
  their active state on Board and Backlog, board card Left/Right keyboard navigation moves focus between
  columns, and the audit log's pagination bar renders with "Previous" correctly disabled on page 1).
- **Batch 13 (done): custom fields** (D9, deferred-after-V1, item 6 from the same user-picked list as
  Batch 12) -- admin-defined fields scoped to a single project, shown on ticket create/edit/view.
  Deliberately scoped down from D9's full target: one context per field (its project, not also its issue
  type -- D9 already rules out named screens/screen schemes, and per-issue-type contexts on top of that
  would multiply this batch's scope well past what was asked for), a fixed always-shown-everywhere
  visibility (no per-stage hidden/show-on flags), and no per-field default value. Six field types (text,
  number, date, checkbox, single_select, multi_select); every value is a single string on the wire and in
  storage, even for multi_select (comma-joined) -- the same convention `labels` already uses, chosen to
  avoid a second "value is sometimes an array" JSON shape only custom fields would need. New `custom_fields`
  (the field catalog, project-scoped, `UNIQUE(project_id, name)`) and `ticket_custom_field_values`
  (`ON DELETE CASCADE` on both `ticket_id` and `field_id`) tables (migration `018_custom_fields.sql`); new
  `IDatabase::listCustomFields`/`createCustomField`/`findCustomFieldById`/`editCustomField`/
  `deleteCustomField`/`listTicketCustomFieldValues` on both adapters, mirroring the existing
  `ProjectComponent` (D19) methods almost exactly; values are set only as part of `createTicket`/
  `editTicket` (full-replacement, exactly like `labels`) via a private per-adapter helper, not a separate
  public write method. New `GET`/`POST`/`PATCH`/`DELETE /api/v1/projects/{key}/custom-fields[/{id}]`
  (project-Admin-or-above to write) and `GET /api/v1/tickets/{key}/custom-fields` (standard read access)
  routes. `TicketService::requireCustomFieldsSatisfied` rejects a create/edit missing a value for a
  `required` field. `web/`: a new "Custom fields" admin modal on the Projects screen (mirrors the existing
  Components modal), dynamic inputs in the create-ticket modal and the ticket drawer's edit form
  (`customFieldInputMarkup`/`collectCustomFieldValues`, shared by both), and a new "Custom fields" sidebar
  panel in the ticket drawer showing current values (view mode) or editable inputs (edit mode).

  Two real bugs were found and fixed during this batch's own verification, both outside the custom-fields
  code itself: (1) a **pre-existing, nondeterministic SQLite integration test** (`tests/
  sqlite_integration_tests.cpp`) that searched for "the assignee history entry" via a plain `find_if`
  without disambiguating which of two same-second `assignee`-field history rows it meant -- when the two
  rows tied on `created_at`, `ORDER BY created_at DESC, id DESC` broke the tie by random UUID, so the test
  passed or failed depending on UUID comparison outcome. This was almost certainly the real cause of the
  "transient" test failures noted in earlier verification passes (previously guessed to be a `/tmp` file
  race). Fixed by searching for the specific entry whose `newValue` matches the assignment the test
  actually cares about. (2) A **pre-existing CSS overflow**: `.project-card-actions` had no `flex-wrap`,
  so a project card's action-button row silently clipped its last button once a project had enough buttons
  -- unnoticed until this batch's new "Custom fields" button became the fifth. Fixed with `flex-wrap: wrap`.

  New test coverage: `tests/sqlite_integration_tests.cpp` covers full custom-field-definition CRUD
  (creation, duplicate-name rejection, invalid-`fieldType` rejection via the `CHECK` constraint, JSON
  options round-tripping, sort-order assignment/editing), setting/reading/clearing ticket values through
  `createTicket`/`editTicket` (including the full-replacement-clears-omitted-fields behavior), rejecting an
  unknown field id, and cascade-delete of stored values when a field definition is deleted. Verified: full
  rebuild and `ctest` clean in all three build configurations; live-verified over real HTTP against a fresh
  PostgreSQL database (field CRUD, required-field-missing 400, ticket creation/edit with values, field
  deletion cascading away a ticket's stored value); a full Playwright/Chromium browser pass against a fresh
  SQLite database (11/11 checks, re-run against a genuinely fresh single server instance after an earlier
  run's result was caught as untrustworthy -- a leftover server process from a prior verification attempt
  had kept accumulating state across "fresh" database directories since a later `nohup` start silently
  failed to bind the already-in-use port). See `docs/VERIFICATION.md`.

- **Batch 14 (done): outbound webhooks (D39/D41) and outbound email (D52)** -- the remaining two items
  from the same user-picked list as Batch 12/13, both needing a durable delivery mechanism first
  (`CLAUDE.md`: "Durable side effects must eventually use jobs/outbox/events. Do not use detached in-memory
  tasks for email, webhooks..."). Delivery is split the same way `migrate`/`backup`/`restore`/`seed-demo`
  already are: the server (and every request handler) only ever writes fast, local, durable outbox rows --
  zero outbound network I/O in the request path -- and a new `ticket-hub-cli process-outbox` command,
  intended to be admin-cron-scheduled every 1-5 minutes, is the *only* place in the entire codebase that
  makes an outbound network call. Two concrete tables (`webhook_deliveries`, `email_deliveries`), not one
  generic "outbox_events" table, matching this codebase's existing preference for purpose-specific tables
  over a generic wrapper. New migration `019_outbox_delivery.sql` (`webhook_subscriptions`,
  `webhook_deliveries`, `email_deliveries`).
  - **Outbound webhooks.** Admin-configured subscriptions (`POST/GET /api/v1/webhooks`,
    `DELETE /api/v1/webhooks/{id}`, global-administrator-only like the audit log), each with its own
    generated signing secret shown exactly once at creation (matching how personal access tokens already
    work), an optional single-project filter, and an optional event-type filter (empty = every event) drawn
    from a fixed catalog: `ticket.created`, `ticket.status_changed`, `ticket.updated`, `comment.added` --
    deliberately scoped down from D41's full type/status/priority/assignee/custom-field visual filter
    matrix. Payloads are hand-built JSON (no new dependency on `crow::json` inside `application/`, which
    must not depend on `web/`) and signed with `X-TicketHub-Signature: sha256=<hex-hmac>`
    (GitHub/Stripe-style) using a new hand-rolled `Common::hmacSha256Hex`, built on an extended version of
    the project's own existing hand-rolled SHA-256 rather than adding an OpenSSL/libcrypto dependency for
    one construction -- verified against RFC 4231 test vectors 1, 2, and 6. `CURLOPT_FOLLOWLOCATION = 0` on
    every delivery attempt so a compromised/malicious webhook target can't redirect the signed payload to
    an unintended host (SSRF defense).
  - **Outbound email.** A pluggable SMTP backend (`TICKETHUB_SMTP_HOST`/`_PORT`/`_USERNAME`/`_PASSWORD`/
    `_FROM`/`_USE_TLS`) for the existing fixed in-app notification set (D14) -- email enqueueing mirrors the
    in-app notification set one-for-one, gated only on installation-wide SMTP configuration (no per-user
    notification-preference toggle; D86 already ruled those out elsewhere). Subject line and recipient
    address are `stripCrLf()`-sanitized before building the raw SMTP message, since the subject is built
    from user-controlled `ticket.summary` and an unsanitized value would allow email header injection (e.g.
    a forged `Bcc`).
  - **New dependency: libcurl**, linked only into the `ticket-hub-cli` target (not `ticket-hub-core` or the
    `ticket-hub` server binary) -- supports both HTTP (webhooks) and SMTP (email) through one library,
    keeping the server's own dependency and attack surface unchanged. Retry policy is fixed, not
    exponential: `Domain::MaxDeliveryAttempts = 10`, `Domain::DeliveryRetryDelayMinutes = 5`; a delivery
    that exhausts its attempts is marked permanently `failed` (terminal, no manual-retry API in this batch).
    Webhook signing secrets and SMTP credentials are a deliberate, documented exception to "store
    token/session verifiers as hashes": HMAC signing needs the raw key at send time (unlike a bearer-token
    comparison), so the secret is stored in cleartext in the database the same way the database connection
    string itself already is.

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
  assertion in `tests/sqlite_integration_tests.cpp` checks `last_error`'s actual text content (via a new
  `scalarText` test helper) for both the webhook and email failure paths, which the original test block did
  not do.

  New test coverage: `tests/sqlite_integration_tests.cpp` covers webhook subscription CRUD (secret
  generation, event-type/project-filter round-tripping), delivery enqueue/list/record-result for both the
  success and exhausted-retry paths (including the `last_error` content regression check above),
  cascade-delete of a subscription's deliveries (`ON DELETE CASCADE`), and the equivalent email-delivery
  lifecycle; `tests/crypto_tests.cpp` gained 4 RFC 4231 HMAC-SHA256 vector tests. Verified: full rebuild and
  `ctest` clean in all three build configurations. Live-verified end-to-end over real HTTP/SMTP against both
  a fresh SQLite database and a fresh PostgreSQL database: a real local HTTP receiver and a real local SMTP
  receiver (`aiosmtpd`) confirmed the compiled `ticket-hub-cli process-outbox` binary actually delivers --
  correct HMAC-SHA256 signature (independently recomputed in Python), correct email content -- and marks
  both rows `delivered`/`sent`; a second subscription pointed at an unreachable target, and an
  intentionally-unset SMTP host, confirmed the failure/retry path (`last_error` populated correctly,
  `attempt_count` incrementing, terminal `failed` status at `MaxDeliveryAttempts`) on both databases. A
  Playwright/Chromium browser pass against a fresh SQLite database covered the new admin "Webhooks" screen:
  creating a subscription (secret-once banner), the subscription list, and delete. See
  `docs/VERIFICATION.md`.
- **Batch 15 (done): REST write idempotency keys (D128, deferred-after-V1)** -- requested from a follow-up
  menu offered after Batch 14 closed out the original six-item list ("napis mi seznam moznych novych
  funkcionalit a ja se rozhodnu" again; the user picked exactly this one item, "pouze 1"). An optional
  `Idempotency-Key` request header, opt-in per request: only a request that actually carries the header is
  ever looked up or cached, every other request is entirely unaffected. Scoped to the five POST routes
  proven to risk a duplicate *record* on client retry -- `POST /api/v1/tickets`, `POST /api/v1/projects`,
  `POST /api/v1/tickets/{key}/comments`, `POST /api/v1/tickets/{key}/worklogs`,
  `POST /api/v1/tickets/{key}/clone` -- not a generic mechanism applied to every write route: PATCH/DELETE/
  bulk/status-change routes already converge to the same end state on repeat, so there is no duplicate
  *record* for a key to prevent there.
  - **Backend.** New `idempotency_keys` table (migration `020_idempotency_keys.sql`, `PRIMARY KEY (user_id,
    idempotency_key)` -- scoped per caller, so two different users coincidentally choosing the same key
    value never collide) and `IDatabase::findIdempotencyRecord`/`recordIdempotencyResult` on both adapters,
    with thin `TicketService` pass-throughs (no project/global-admin gating needed -- scoped to the caller's
    own userId by construction, and the underlying action already enforces its own authorization before
    either is ever called). `recordIdempotencyResult` is deliberately best-effort (`INSERT OR IGNORE` /
    `ON CONFLICT DO NOTHING`): a narrow concurrent-retry race (two requests carrying the same key arriving
    before either has stored its result) is silently ignored rather than raised, since the caller's actual
    response was already computed and returned either way. New `Api.cpp` helpers `idempotencyReplay`/
    `recordIdempotentResult`, wired into each of the five routes: a request carrying a previously-unseen key
    proceeds normally and its 2xx response is cached afterward (a non-2xx response is never cached -- if the
    original attempt failed validation, no side effect happened, so a retry with the same key should simply
    run normally, not replay a cached error forever); a request whose key already has a cached record with a
    *matching* SHA-256 hash of `route path + "\n" + request body` gets that cached response replayed
    (`Idempotency-Replayed: true` header added); a mismatched hash (the same key reused for a genuinely
    different request, or accidentally reused across two different routes/tickets) gets `409`, never a
    nonsensical replay -- hashing the route path together with the body (not just the body) is what makes
    that last case safe even when two different routes' bodies coincidentally match.
  - **Frontend.** The demo web UI's own five equivalent forms/buttons (create-ticket, create-project,
    add-comment, add-worklog, clone-ticket) send a fresh `crypto.randomUUID()` as the header on every
    submit via a new `idempotencyHeaders()` helper, and each submit control is disabled for the duration of
    its request -- the two defenses are complementary, not redundant: disabling the button is what actually
    stops a literal double-click from firing two requests in the first place (the idempotency key can't
    prevent that on its own, since a fresh key is minted per handler invocation), while the key is what
    protects the case a double-click guard can't cover -- a successful response that never reaches the
    browser (dropped connection, closed tab) followed by the user manually retrying once the control is
    re-enabled.
  - **A real pre-existing bug was found and fixed while wiring the frontend**, unrelated to idempotency
    logic itself: `api()`'s `fetch(path, { headers, ...options })` call spread `...options` *after*
    `headers`, so any caller that itself passed an `options.headers` key would have that silently clobber
    the function's own carefully-merged `Content-Type`/`X-CSRF-Token` headers -- invisible until this
    batch's `idempotencyHeaders()` became the first caller ever to pass one, at which point every wired
    create action returned `403 Missing or invalid CSRF token` (caught immediately via a live browser
    verification pass, not shipped). Fixed by reordering to `fetch(path, { ...options, headers })`, so the
    function's own merged `headers` always wins.

  New test coverage: `tests/sqlite_integration_tests.cpp` covers a lookup miss (`nullopt`, not an error), a
  stored record round-tripping its hash/status/body exactly, the same key value used by two different users
  being unrelated records, and `recordIdempotencyResult`'s best-effort duplicate-write semantics (a second
  write for an already-recorded key is silently ignored, keeping the original stored result rather than
  overwriting it). Verified: full rebuild and `ctest` clean in all three build configurations. Live-verified
  over real HTTP against both a fresh PostgreSQL database and a fresh SQLite database: the happy-path replay
  (identical second response, `Idempotency-Replayed: true`, no duplicate row created -- confirmed via both
  the API list and a direct database row count), the same-key-different-body `409`, the cross-route
  collision defense (the same key reused on a different route also correctly `409`s rather than replaying
  the wrong resource), the oversized-key `400`, and the "failed attempt is never cached, so a corrected
  retry with the same key just succeeds" case -- confirmed via the raw `idempotency_keys` table only ever
  containing the two *successful* attempts, never the intermediate `400`. A full Playwright/Chromium browser
  pass against a fresh SQLite database exercised all five wired UI actions (create ticket, create project,
  add comment, log time, clone) end-to-end with no console errors and no duplicate records, confirmed the
  submit-button-disabled-during-request behavior, and is what caught the `api()` spread-order bug above
  before it could ship. See `docs/VERIFICATION.md`.

- **Batch 16 (done): fix -- no way to view or unarchive an archived project (D87)** -- user-reported bug,
  not new scope ("kde najdu archived projects a jak udelat unarchive" -- "where do I find archived
  projects and how do I unarchive one"). Archiving a project removed it from `GET /api/v1/projects` (the
  only project list the UI ever fetched, correctly excluding archived projects per D87) with no substitute
  view anywhere -- the recycle bin only holds soft-deleted projects, and there was no second API route for
  archived ones. `projectJson()` also never serialized `archived`, so the pre-existing per-card
  Archive/Unarchive button's client-side check was always `undefined` -- dead code independent of the
  missing-view problem.
  - **Backend.** New `IDatabase::listArchivedProjects()` (SQLite and PostgreSQL, mirroring the existing
    `listDeletedProjects()`/`ProjectSelectSql` pattern) and `TicketService::listArchivedProjects(actor)` --
    `requireReadAccess`, the same rule as `listProjects` (any authenticated reader, or anonymous if that
    installation toggle is on), deliberately not `requireGlobalAdmin` like the recycle bin's
    `listDeletedProjects`, since D87 says an archived project stays viewable even though it "leaves active
    lists." New `GET /api/v1/projects/archived` route. `projectJson()` now includes `archived`.
  - **Frontend.** `renderProjectsView` changed from a `showingDeleted` boolean to a `viewMode` string
    (`'active' | 'archived' | 'deleted'`); the Projects page gained a "📦 Archived" toggle (visible to any
    reader, alongside the existing admin-only "🗑 Recycle bin" toggle) whose cards show a working
    "Unarchive" button.

  New test coverage: `tests/authorization_integration_tests.cpp`'s existing D87 block gained a check that a
  non-admin reader sees an archived project via `listArchivedProjects` with `archived == true`, and that
  unarchiving removes it from that list again. Verified: full rebuild and `ctest` clean (8/8) in the
  SQLite + Crow-server configuration; PostgreSQL could not be compiled in this environment (`libpq-dev` is
  not installed, only the runtime `libpq5`) -- the Postgres change mirrors the same existing pattern
  structurally but is unverified by a real compile here. Live-verified end-to-end over real HTTP against
  the actual compiled server and a demo-seeded SQLite database (the Chrome browser extension needed for a
  Playwright pass was not connected in this environment, so `curl` was used against the same routes the UI
  calls instead): archiving moved a project from the active list to the archived list and back on
  unarchive; a non-admin got `403` archiving a project they don't administer but `200` reading the archived
  list, confirming the read-access-not-admin-gated design over the wire. See `docs/VERIFICATION.md`.

- **Batch 17 (done): fix -- every lead/assignee picker was hardcoded to the three demo accounts** --
  user-reported bug, not new scope ("proc nemohu dat sebe jako lead nebo default assignee v Componenets a
  vidim tam tri ucty ktere jsem nezakladat" -- "why can't I set myself as lead or default assignee in
  Components, and I see three accounts I didn't create"). Every user-picking `<select>` in the web UI --
  Components' Lead/Default assignee, ticket Assignee on create and edit, bulk assign, the Tickets/Backlog
  assignee filters -- hardcoded the same three seeded demo accounts (`demo`/`alex`/`sam@ticket-hub.local`)
  across a `DEMO_USERS` constant plus five separately-duplicated inline `<option>` lists, instead of the
  real user directory (`GET /api/v1/users`, already fetched into `state.users` for @mention autocomplete
  but unused by any of these pickers). No account beyond the three demo users -- including a real
  administrator's own -- could ever be selected anywhere in the app; purely client-side, since the backend
  write paths already accepted any valid user email.
  - **Frontend.** One shared `userSelectOptions(selectedEmail, emptyLabel)` helper built from `state.users`
    replaces the hardcoded constant and all five duplicated lists (the empty-option label is now a
    parameter -- "None" for Components, "Unassigned" for assignee pickers, "Any assignee" for the filters).
    The create-ticket modal's assignee `<select>` is static HTML in `index.html` (no `state.users` at
    page-parse time), so it now ships with just an "Unassigned" placeholder and `openCreateModal()`
    repopulates it via the same helper on every open, matching the pattern already used there for
    parent/component/custom-field options.

  No C++/schema change -- frontend-only. Verified: full rebuild and `ctest` clean (8/8) as a sanity check.
  Live-verified end-to-end over real HTTP (the Chrome browser extension was still not connected, so `curl`
  again): created a new admin account via `ticket-hub-cli create-user`, confirmed `GET /api/v1/users`
  returned it, confirmed the served `/app.js` has zero remaining hardcoded demo-account strings, and proved
  the full path -- not just data availability -- by setting the new account as a project component's
  `leadEmail` through the real API and confirming it stuck. Cleaned up the throwaway component/user
  afterward. See `docs/VERIFICATION.md`.

- **Batch 18 (done): web admin user management (D2/D53/D57)** -- user-requested, asked right after batch 17
  where accounts could be managed as an admin. Not new scope: a decided-but-never-implemented gap, same
  class as batch 8's five-decision audit. Decision 2 (admin-created accounts only, no self-registration)
  preserved; Decision 53 ("admin-performed reset, sets a temporary password") and Decision 57
  ("deactivation only, no anonymize/merge") implemented for the first time. `users.active`/`is_admin`
  already existed in the schema and were already enforced at login (`AuthService::login`/`validateSession`
  already reject a deactivated user) -- simply never exposed for editing after account creation, so no
  migration was needed.
  - **Backend.** `IDatabase::setUserActive`/`setUserAdmin`/`setPasswordHash`/`deleteAllSessionsForUser` on
    both adapters. `AuthService` gained a private `requireGlobalAdmin` and five public methods:
    `adminListUsers`, `adminCreateUser` (shares `createUser`'s validation via a refactored free function,
    now attributing the audit event to the acting admin instead of leaving it actor-less), `adminSetUserActive`,
    `adminSetUserAdmin`, `adminResetPassword` (returns a fresh temporary password, shown once, like a PAT's
    raw token; also clears any failed-login lockout and invalidates every existing session for that user).
    New `GET`/`POST /api/v1/admin/users`, `PATCH .../active`, `PATCH .../admin`,
    `POST .../reset-password` routes and an `adminUserJson()` serializer (the pre-existing
    `userDirectoryJson` stays narrower -- it backs the @mention/assignee-picker directory every reader can
    see, not an admin view).
  - **Two conservative defaults, undecided by any existing decision text** (documented here rather than a
    separate ADR -- this project has never used standalone ADR files, following the same convention as
    D54's "sign out everywhere" default): an administrator can never deactivate their own account or remove
    their own admin privileges through this action (`400`, not merely discouraged); the reset-generated
    temporary password is auto-generated and shown once rather than admin-typed, matching the existing
    PAT/webhook-secret pattern.
  - **Frontend.** New admin-only "Users" nav item and `renderUsersAdmin()` view, mirroring `renderWebhooks`'s
    list + create-form + reveal-once-banner structure; Deactivate/Remove-admin buttons are disabled
    client-side for the caller's own row too, matching the server-side rules.

  New authorization-integration-test coverage: every action rejected for a non-admin actor, both
  self-protection rules, not-found returns `false`/`nullopt` rather than throwing, deactivation and
  password reset each immediately invalidating an already-issued session, a deactivated account failing
  login, and the new temporary password actually working for a fresh login. Verified: full rebuild and
  `ctest` clean (8/8) in the SQLite + Crow-server configuration; PostgreSQL still could not be compiled here
  (`libpq-dev` missing). Live-verified end-to-end over real HTTP: created/listed accounts, confirmed both
  self-protection `400`s, deactivated an account and confirmed its subsequent login attempt returns `401`,
  reset its password and confirmed a fresh temporary password was returned, and confirmed a non-admin gets
  `403` on every admin route. Cleaned up the throwaway account directly via SQLite afterward (no delete-user
  route exists by design -- deactivation, not deletion, per D57). See `docs/VERIFICATION.md`.

- **Batch 19 (done): Kanban board drag-and-drop reordering within a column** -- user-requested ("proc
  nemohu zmenit poradi ticketu na boardu?"). The board's cross-column drag (batch 3) explicitly no-op'd a
  drop back into the ticket's own column; manual reordering (D31) already existed but only via the
  Tickets/Backlog screens' ↑/↓ buttons.
  - Board columns are now sorted by `rankOrder` client-side, matching Tickets/Backlog's own sort -- needed
    for a stable per-column drop position (the board's fetch uses the API's default `updated_at DESC`
    order, not rank).
  - New `boardDropInsertionBeforeKey()` computes a `beforeTicketKey` from where the cursor sits relative to
    the column's cards; a same-column drop now calls the existing `POST /api/v1/tickets/{key}/reorder`
    route (D31) via a new `applyBoardReorder()` -- no new backend code. A drop landing at the card's actual
    current position is detected and skipped.
  - No new keyboard path needed -- the Tickets/Backlog ↑/↓ buttons already cover it, same reasoning as the
    pre-existing cross-column drag.

  No C++/schema change -- frontend-only. Verified: full rebuild and `ctest` clean (8/8) as a sanity check.
  Live-verified the exact request the new handler sends, over real HTTP: reordered two same-status seeded
  tickets (`TH-5`/`TH-6`), confirmed the swap persisted through a fresh fetch, restored the original order.
  See `docs/VERIFICATION.md`.

- **Batch 20 (done): fixed Story Points picker and adaptable ticket detail drawer** -- the two explicitly
  queued post-V1 follow-ups. Create and edit now offer the fixed Jira-style scale `0`, `0.25`, `0.5`, `1`,
  `2`, `3`, `5`, `8`, `13`, `20`, `40`, `100`, with an approximate relative-effort explanation beside
  the control.
  Historic nonstandard values remain selectable during an edit rather than being silently cleared. The
  ticket drawer has an accessible left-edge resize control (pointer drag, Arrow keys, Shift+Arrow for larger
  steps, Home/End for limits), persists its normal width in `localStorage`, and has a full-screen toggle;
  Escape leaves full-screen before closing the drawer. Frontend-only, no schema or API change.

## Not yet built (still V1 scope — see `REDUCED_SCOPE_ROADMAP.md`)

- Phases 1-8 (the entire reduced-scope V1 roadmap) are complete -- nothing remains in this category.
  Everything below this point is either post-V1 optional follow-up (above) or permanently out of scope.
- **Milestone 3 (Phases 6 and 7) is fully complete.** Pagination (D126) covers `GET /api/v1/tickets`,
  notifications, and the admin audit log. Every other list endpoint (projects, comments, worklogs,
  attachments, sessions, tokens, watchers, voters, board-columns, ticket-links, comment-reactions) remains
  unpaginated; extending it further is optional follow-up, not a blocker to Phase 6's exit gate.
- **Phase 8 (Milestone 4) is fully complete**, which closes the entire reduced-scope V1 roadmap: Docker
  packaging (D50), light/dark theme (D46), the accessibility baseline pass (D47), the browser-support note
  (D139), and the threat-model/security self-review (`docs/THREAT_MODEL.md`) are all done.

## Permanently out of V1 scope (do not build these)

OIDC, invitations, public registration, configurable permission/notification schemes, the configurable
workflow engine, saved/shared filters, Scrum/sprints/agile reports, versions/releases, ticket templates,
automation rules, service accounts, Git integration, the Jira migration tool, CSV import, inbound email,
the background job queue, internal event bus, realtime
(SSE), in-memory cache, S3 attachment storage, Kubernetes/Helm/`.deb`/`.rpm` packaging, the i18n
framework, and pluggable secrets/observability backends. Full list with decision numbers:
[REMOVED_AND_DEFERRED_FEATURES.md](REMOVED_AND_DEFERRED_FEATURES.md).

Do not treat the current UI or `IDatabase` shape as the final API. They are a working vertical slice
used to evolve the architecture incrementally.
