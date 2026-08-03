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
  links/watch-vote/delete in the issue drawer, project management, the issue recycle bin, and reorder/
  move/bulk actions -- all browser-verified with Playwright/Chromium (`docs/VERIFICATION.md`).
- PostgreSQL and SQLite database adapters behind one application-facing interface.
- Ordered checksummed schema migrations and idempotent demo seed.
- Projects and transactional project-local issue numbering.
- Fixed issue types/statuses/priorities used by the prototype.
- Issue creation/list/detail, status changes, labels and comments -- now principal-driven, not the fixed
  demo user (Phase 1).
- Issue version exposed for optimistic status-change conflict detection.
- Permanent issue-key alias lookup, now actually written to by `moveIssue` (D37/D38, Phase 3).
- Recycle-bin columns and live-query filtering, now a real recycle bin for both projects and issues.
- **Local-account identity (Phase 1):** UUID/email users (no `handle` yet), Argon2id password hashing,
  server-side sessions (SHA-256 token hash, 30-day fixed lifetime), minimal login-attempt lockout,
  administrator-only account creation via `ticket-hub-cli create-user`. No self-registration, no
  invitations, no OIDC, no forced-password-change flow -- see `REDUCED_SCOPE_SPECIFICATION.md` section 3.
- **Fixed project-role authorization and project lifecycle (Phase 2):** Viewer/Member/Admin roles plus a
  global-administrator bypass, enforced on every issue and project write; project create (global admin),
  archive/unarchive (project admin), soft delete/restore/permanent delete (project admin to bin, global
  admin to restore or purge), fixed 90-day on-demand recycle-bin retention; installation-wide anonymous
  read-access toggle, off by default (D59) -- every read use case now takes an optional `Principal`.
- **Fixed hierarchy and workflow rules (Phase 3, complete at the core/CLI/test layer):** the Epic ->
  Story/Task/Bug -> Sub-task hierarchy is enforced on issue creation (D5, D29, D64-D66); status
  transitions enforce the fixed workflow rules (D68-D70) -- resolution required on completion, cleared on
  reopen, and a parent cannot complete while any sub-task is unfinished; full-replacement issue edit with
  the same optimistic-locking contract as status changes (D129) -- summary, description, priority,
  assignee, story points, due date, labels, one `issue_history` row per changed field; the fixed
  issue-link catalog (D17) -- `blocks`/`relates_to`/`duplicates`/`clones`, visible from both ends,
  requiring project-Member-or-above on both linked issues' projects; simple field-copy cloning (D60) with
  an automatic `clones` link; self-service watching (D20) and voting (D79), the one issue write with no
  project-role check; the issue recycle bin (D22, mirrors the project one) -- soft delete by project
  admin, restore/list/permanent-delete by global admin only; simple bulk actions (D36) --
  status/assignee/label/recycle applied to a list of issue keys, each through the same single-issue
  operation, reporting partial success rather than rolling back; simple integer manual ordering with
  full renumbering on every move (D31), replacing the never-used `issues.rank_value` LexoRank placeholder;
  moving an issue to a different project (D37) -- no compatibility check needed since every project
  shares the same fixed types/workflow/fields, so a move is a `project_id` change plus a freshly
  allocated key/number, rejected if the issue has a parent or any children, requiring
  project-Member-or-above on both the source and target projects, with the vacated key becoming a
  permanent alias (D38). See "Not yet built" below for the rest of Phase 3.
- Domain, migration, crypto, identity, SQLite integration, authorization, and workflow tests (7/7
  passing; see `docs/VERIFICATION.md`).
- **Comment editing and tombstone delete (Phase 4, partial, D81/D82/D83):** an `edited_at` timestamp
  instead of a version-history table; soft-delete via the same `deleted_at`/`deleted_by_user_id` columns
  issues and projects already use, with no separate admin recycle-bin API for comments (the row and
  original body simply remain in the database, excluded from ordinary listing); simplified permissions --
  the comment's own author can always edit/delete it, otherwise the actor needs project-Admin-or-above (or
  global admin), no separate edit-own/edit-all/delete-own/delete-all matrix. Same optimistic-locking
  contract (`expectedVersion` -> `Domain::ConcurrencyConflict`, 409) as issue edits.
- **Fixed emoji reactions on comments (Phase 4, partial, D84):** a `comment_reactions` many-to-many table
  (mirroring `issue_watchers`/`issue_votes`) with a fixed eight-key reaction set (GitHub's own set,
  chosen as a conservative default since the decision register does not enumerate one); self-service,
  no project-role check, same reasoning as watch/vote (D20/D79); idempotent add/remove.
- **@mention handles and the fixed in-app notification set (Phase 4, partial, D56/D80/D14):** an optional,
  unique `users.handle` (admin-set at account creation, no self-service profile editing yet); comments
  are scanned once, at creation, for `@handle` tokens, notifying each resolved user; three fixed
  notification types only -- assigned, mentioned, comment on a watched issue -- no email, no
  admin-configurable schemes, no per-user preferences/digests; a recipient who is both mentioned and
  watching the same comment gets one notification, not two. `GET /api/v1/users` backs @mention autocomplete
  in the comment textarea; a notification bell with an unread badge in the UI opens a panel that marks
  notifications read on click and navigates to the related issue.
- **Rendered Markdown, visual toolbar, and live preview (Phase 4, D16):** comment bodies and issue
  descriptions render as formatted HTML (bold/italic/inline code/links/headings/lists/blockquotes/fenced
  code/rules) via a deliberately small, safe-by-construction subset -- input is HTML-escaped first, then
  wrapped in a fixed set of hardcoded tags, so there is no separate sanitization pass to get wrong.
  Bold/Italic/Code/Link/list/Quote toolbar buttons plus a live-preview toggle on every Markdown-capable
  textarea.
- **Simplified worklogs (Phase 4, D12/D13):** time spent + an optional comment only, no remaining-estimate
  linkage (D12 dropped time estimates from V1 entirely); no own-vs-others edit/delete permission split
  (D13) -- any project member with issue access may edit or delete any worklog on that issue, not just
  the one they logged themselves, unlike comments' author-or-admin rule (D83). Tombstone delete and the
  same optimistic-locking contract as comment/issue edits.
- **Simple append-only admin/security audit log (Phase 4, D23):** an `audit_events` table with no
  categories-as-a-retention-feature, export, or configurable retention -- rows are never purged.
  Recorded automatically for a small, focused set of admin/security-relevant writes (failed/blocked
  login, user creation, the anonymous-read toggle, permanently deleting a project or issue), not as a
  general-purpose hook on every write. Global-administrator-only read access, via a new "Audit log" nav
  item in `web/`. **This closes out Phase 4 (Collaboration) -- every item in
  `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 4 list is now implemented.**
- **Ad-hoc issue filter/search widening (Phase 5, D10/D43):** `GET /api/v1/issues` and `Domain::IssueFilter`
  now also support type/priority/assignee/label/due-date filters (in addition to project/status/search),
  and search now also matches issue description, not just summary/key. Still ad-hoc, in-UI-only filters
  (no saved/shared filters, no JQL, not usable as a webhook/board source) and still a plain `LIKE`/`ILIKE`
  substring match (no full-text index). This is the first Phase 5 slice.
- **Personal dashboard widgets (Phase 5, D24):** `GET /api/v1/dashboard` now returns `assignedToMe`,
  `watchedIssues`, and `upcomingDeadlines` for an authenticated caller (empty for anonymous). Matches
  D24's fixed widget set (assigned issues, watched issues, recent activity, deadlines, simple stats) minus
  the active-sprint widget, dropped since Scrum was removed for V1. `assignedToMe`/`upcomingDeadlines`
  exclude Done-category issues.
- **Kanban board WIP limits (Phase 5, D32/D33):** `GET /api/v1/board-columns` and
  `PUT /api/v1/board-columns/{statusKey}` (global-administrator-only). A single flat, installation-wide
  table -- one row per fixed workflow status, no per-project scoping at all, following
  `docs/REDUCED_SCOPE_DATA_MODEL.md`'s target schema literally. Soft, display-time-only: an over-limit
  column is highlighted in `web/`'s Board view, never blocked from receiving more issues.
- **Attachments (Phase 5, D15/D98-D105):** the full vertical -- upload, download, delete, and a recycle
  bin, all via `/api/v1/issues/{key}/attachments` plus `/api/v1/attachments/{id}/...`. Local filesystem storage
  only, hardwired (D15, no S3/pluggable backend). Fixed limits (D98): 25MB/file, 20/issue, a blocked-
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
- **Read-only CSV export of issues (Phase 6, D48):** `GET /api/v1/issues/export.csv`, sharing
  `Domain::IssueFilter`'s query-parameter filters and authorization with `GET /api/v1/issues`. No CSV
  import, no Jira migration tool. `web/`'s Issues view gained an "Export CSV" link matching the current
  filters.
- **Fixed request-body/bulk-item constants (Phase 6, D125):** every JSON request body capped at 1 MiB
  (`413` if exceeded); every bulk-action `issueKeys` array capped at 200 items (`400` if exceeded). No
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
  no other issues.
- **Numbered/offset pagination (Phase 6, D126):** `GET /api/v1/issues` accepts `page`/`pageSize` (default/
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
  keyboard-operability gap -- issue table rows, Kanban board cards, project cards, and inline issue-key
  cross-reference links were mouse-click-only, unreachable by keyboard. Fixed with a shared
  `makeKeyboardActivatable` helper (`tabindex="0"`, `role="link"`, `Enter`/`Space` handling) plus a visible
  focus-ring CSS rule. No formal WCAG audit -- not required by D47's reduced V1 scope. D139 (browser
  support) reconfirmed satisfied by construction, no code change. Browser-verified with Playwright/
  Chromium (keyboard-only activation of each fixed element, no mouse-click regression) plus a clean
  regression re-run.
- **Threat-model / security self-review (Phase 8):** full write-up in `docs/THREAT_MODEL.md`. Found and
  fixed a real broken-access-control (IDOR) bug -- `editComment`/`deleteComment`/`editWorklog`/
  `deleteWorklog`/`deleteAttachment` checked the caller's project role against the URL's issue but looked
  up the target resource purely by id, letting a user with a role on one project reach a comment/worklog/
  attachment belonging to a different project by routing through their own issue's URL. Also fixed: a
  CSRF cookie that was an unnecessary literal prefix of the session token, a login timing side channel
  enabling email enumeration, a missing CSRF check on `/api/v1/auth/logout`, and CSV formula injection in
  the export route. New regression tests cover the IDOR fix; every fix reproduced and confirmed live over
  HTTP. **This closes Phase 8, Milestone 4, and the entire reduced-scope V1 roadmap.**

## Post-V1 (optional, non-roadmap follow-up, picked one at a time by explicit user choice)

- **Batch 1 (done): account settings web UI** -- personal access tokens and active sessions management,
  both already had a complete REST API since Phase 6, only the `web/` surface was missing.
- **Batch 2 (done): re-typing and re-parenting an issue after creation** -- the one item left open since
  Phase 3. `editIssue` now edits `issueTypeKey`/`parentIssueKey` too, re-validating the fixed hierarchy
  shape and rejecting a hierarchy-level retype while the issue has children (checked transactionally,
  same precedent as `moveIssue`'s own "has children" rule). See `docs/VERIFICATION.md` for detail.
- Remaining optional items, not yet started: drag-and-drop board reordering, a friendlier bulk Done-status
  picker, keyboard multi-select.

## Not yet built (still V1 scope — see `REDUCED_SCOPE_ROADMAP.md`)

- Phases 1-8 (the entire reduced-scope V1 roadmap) are complete -- nothing remains in this category.
  Everything below this point is either post-V1 optional follow-up (above) or permanently out of scope.
- **Milestone 3 (Phases 6 and 7) is fully complete.** Pagination (D126) covers `GET /api/v1/issues` only
  so far -- every other list endpoint (projects, comments, worklogs, attachments, notifications,
  sessions, tokens, audit events, watchers, voters, board-columns, issue-links, comment-reactions)
  remains unpaginated; extending it further is optional follow-up, not a blocker to Phase 6's exit gate.
- **Phase 8 (Milestone 4) is fully complete**, which closes the entire reduced-scope V1 roadmap: Docker
  packaging (D50), light/dark theme (D46), the accessibility baseline pass (D47), the browser-support note
  (D139), and the threat-model/security self-review (`docs/THREAT_MODEL.md`) are all done.

## Permanently out of V1 scope (do not build these)

OIDC, invitations, public registration, configurable permission/notification schemes, the configurable
workflow engine, custom fields, saved/shared filters, Scrum/sprints/agile reports, versions/releases,
issue templates, automation rules, webhooks, service accounts, Git integration, the Jira migration
tool, CSV import, outbound/inbound email, the background job queue, internal event bus, realtime
(SSE), in-memory cache, S3 attachment storage, Kubernetes/Helm/`.deb`/`.rpm` packaging, the i18n
framework, and pluggable secrets/observability backends. Full list with decision numbers:
[REMOVED_AND_DEFERRED_FEATURES.md](REMOVED_AND_DEFERRED_FEATURES.md).

Do not treat the current UI or `IDatabase` shape as the final API. They are a working vertical slice
used to evolve the architecture incrementally.
