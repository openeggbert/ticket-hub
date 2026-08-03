# Changelog

## Unreleased — Rename the "Selected" workflow status to "Confirmed"

- The `selected` status's display name is now "Confirmed" (the internal `status_key` is unchanged, so
  filters/URLs/API calls referencing `selected` are unaffected). Updated in the demo seed
  (`migrations/sqlite/002_seed_demo.sql` and `migrations/postgresql/002_seed_demo.sql` -- an idempotent,
  explicitly re-runnable seed script, not a checksummed schema migration, so it's edited directly rather
  than requiring a new migration) and in `web/app.js`'s client-side `STATUSES` label list. Verified against
  a fresh SQLite seed and a fresh live PostgreSQL seed; the board column header, issue status chips, and
  every status dropdown/filter now show "Confirmed".

## Unreleased — Bulk Done-status picker and keyboard multi-select (post-V1)

- **Bulk status changes now support Done-category statuses**: the bulk status picker no longer excludes
  them, and a resolution picker appears (matching the drawer/board pattern) whenever the selected status
  requires one. The backend already supported a shared `resolution` in the bulk request; only the UI was
  missing.
- **Keyboard-driven multi-select for the issues table**: a "select all" checkbox in the table header;
  Shift+click a row checkbox to range-select between it and the last-clicked one; Shift+ArrowDown/ArrowUp
  on a focused checkbox extends the selection one row at a time from the keyboard alone.
- Both are `web/`-only changes -- no backend, schema, or API changes.
- Fourth and final item of the optional, non-roadmap follow-up list, picked by explicit user choice.

## Unreleased — Kanban board drag-and-drop (post-V1)

- **Drag-and-drop card movement** between board columns: dropping a card on a different column applies
  the status change immediately; dropping on a Done-category column without an existing resolution opens
  a small prompt for one first (same D68-D70 rule the drawer's status dropdown already enforces). Dropping
  a card back on its own column is a no-op.
- Purely a new client for the existing `PATCH /api/v1/issues/{key}/status` route -- no backend, schema, or
  API changes.
- The board's status dropdown in the issue drawer remains the keyboard-operable path; native HTML5 drag-
  and-drop has no built-in keyboard equivalent, and this is documented as a deliberate gap, not a
  regression of the earlier accessibility baseline pass.
- The third item of optional, non-roadmap follow-up, picked by explicit user choice.

## Unreleased — Re-typing and re-parenting an issue after creation (post-V1)

- **Full-replacement issue edit now covers `issueTypeKey`/`parentIssueKey`**: closes the one remaining
  gap left open since Phase 3. Re-typing/re-parenting re-validates the fixed hierarchy shape (Epic/
  Sub-task/same-project/parent-level, D5/D29/D64-D66) exactly like at creation, plus rejects self-
  parenting. Retyping across hierarchy levels (Epic <-> Story/Task/Bug <-> Sub-task) is rejected
  transactionally while the issue has child issues (same "has children" precedent as `moveIssue`);
  same-level retyping (e.g. Task -> Bug) is always allowed. New `issue_type`/`parent` `issue_history`
  rows on change.
- New Type/Parent controls in the issue drawer's edit form, mirroring the create modal's picker.
- Live-verified end-to-end including against a real PostgreSQL instance (not just SQLite, since both
  database adapters changed) and browser-verified with Playwright/Chromium.
- The second item of optional, non-roadmap follow-up, again started only after being asked which to pick.

## Unreleased — Account settings web UI: personal access tokens and active sessions (post-V1)

- **New "Account" page**, visible to every authenticated user: manage personal access tokens (create,
  view status, revoke) and active sessions (view, "sign out everywhere else"). Both already had a
  complete REST API (Phase 6, D39/D40/D54); this adds the `web/` screen that was the one remaining gap
  called out in every V1-completion note. No backend/schema changes.
- The first optional, non-roadmap follow-up item, picked by explicit user choice after V1 closed.

## Unreleased — Threat-model / security self-review — Phase 8 complete, Milestone 4 complete

- **Security**: a dedicated review of the full HTTP attack surface found and fixed five issues -- most
  notably a **broken access control (IDOR)** bug where `editComment`/`deleteComment`/`editWorklog`/
  `deleteWorklog`/`deleteAttachment` checked the project-role requirement against the issue named in the
  URL but looked up the target resource purely by id, letting a user with a role on one project reach and
  mutate a comment/worklog/attachment belonging to a different project by routing the request through
  their own issue's URL. Also fixed: the CSRF cookie was an unnecessary prefix of the session token (now
  generated independently), a login response-time side-channel enabled email enumeration (now equalized
  with a dummy Argon2id verify), `/api/v1/auth/logout` was the one mutating route missing a CSRF check
  (now consistent with the other 45), and the CSV export was vulnerable to spreadsheet formula injection
  (now mitigated with the standard leading-apostrophe fix).
- New `docs/THREAT_MODEL.md`: the full write-up -- scope, method, every finding (fixed and accepted-
  residual-risk), and what was and wasn't covered.
- New regression tests in `tests/authorization_integration_tests.cpp` covering the IDOR fix across all
  three resource types. All fixes reproduced and confirmed live over HTTP before/after.
- **This closes Phase 8 (Milestone 4) and the entire reduced-scope V1 roadmap.**

## Unreleased — Accessibility baseline pass and browser-support note (D47/D139) — Phase 8 continued

- **Accessibility (D47)**: issue table rows, Kanban board cards, project cards, and inline issue-key
  cross-reference links are now keyboard-focusable and operable with Enter/Space (new
  `makeKeyboardActivatable` helper in `web/app.js`), closing the one real keyboard-operability gap found
  in a review of the existing UI. A small CSS rule adds a visible focus ring for the newly-focusable
  elements. Semantic HTML (landmarks, labeled form controls, `aria-label`/`aria-live`/dialog roles) was
  already in place from earlier phases. No formal WCAG audit -- not required by D47's reduced V1 scope.
- **Browser support (D139)**: reconfirmed, no code change -- `web/app.js` uses no framework or transpiled/
  polyfilled syntax, so the last two major versions of Chrome/Firefox/Edge/Safari are supported by
  construction.
- Verified with Playwright/Chromium (keyboard-only activation of each fixed element, confirmed no mouse-
  click regression) and a full regression re-run.

## Unreleased — Light and dark theme (D46) — Phase 8 continued

- **Dark theme**: `web/styles.css` now follows the OS-level `prefers-color-scheme` signal automatically
  (`color-scheme: light dark` plus a full `@media (prefers-color-scheme: dark)` variable-override block).
  No manual in-app toggle or persisted preference -- not called for by D46's "light and dark theme...
  simple" wording.
- Converted ~30 hardcoded literal colors to the existing/new CSS custom-property set so the whole UI
  (chips, banners, modals, the board, tables, buttons) themes consistently; fixed one bug found during
  verification (`.link-form input` had no dark-mode styling and rendered as a stray white box).
- Verified with Playwright/Chromium: light mode confirmed unchanged, dark mode confirmed via computed
  styles plus a five-view screenshot review, and both existing browser regression scripts re-run clean.

## Unreleased — Docker image and Compose distribution (D50) — Phase 8 started

- **Docker**: new two-stage `Dockerfile` (build with the full toolchain, run on a slim
  `debian:bookworm-slim` image with just the required shared libraries), the only supported distribution
  path for V1 -- no `.deb`/`.rpm`, no Helm/Kubernetes.
- **Compose**: `docker-compose.yml` gained a `ticket-hub` app service alongside the existing `postgres`
  one; `docker compose up` alone now brings up the full instance at `http://127.0.0.1:8080`. The existing
  `docker compose up -d postgres`-only workflow is unchanged.
- New `.dockerignore` to keep the build context small.
- Verified as far as this development environment's network policy allows: `docker build --check` and
  `docker compose config` both pass; the exact runtime configuration the container sets was verified
  directly on the host against both SQLite and a live PostgreSQL database. The base-image layer pull
  itself could not be executed here due to a network-egress policy block unrelated to the Dockerfile.

## Unreleased — Structured JSON logs and admin version banner (D112/D133) — Phase 7 complete

- **Structured JSON logs**: every log line Crow produces (server startup, per-request logs, warnings/
  errors) is now one JSON object per line on stdout (`timestamp`/`level`/`service`/`message`), via a new
  `TicketHub::Web::JsonLogHandler` registered as Crow's log handler. No Prometheus, no OpenTelemetry.
- **Admin version banner**: new global-administrator-only `GET`/`PUT /api/v1/settings/latest-known-version`
  and a web UI banner shown to admins when the configured "latest known version" differs from the running
  one. No outbound network calls -- an admin sets the value manually; there is no decision text specifying
  an automatic update-check mechanism and no HTTP-client infrastructure in this codebase to build one on.
- This closes out Phase 7's entire roadmap list.

## Unreleased — Backup and restore (D106-D108) — Phase 7 started

- **Backup**: `ticket-hub-cli backup <output-directory>` copies the attachments directory and dumps the
  database into `<output-directory>` (SQLite: online backup API; PostgreSQL: `pg_dump --clean
  --if-exists`). Offline/maintenance-window use only; refuses to write into a non-empty directory.
- **Restore**: `ticket-hub-cli restore <backup-directory> --yes` overwrites the current database and
  attachments directory with the backup's contents, then runs pending migrations. Requires the explicit
  `--yes` flag; without it, prints a warning and refuses to proceed. Direct restore into the target, no
  isolated staging environment; the admin is responsible for their own pre-restore backup.
- Confirmed `ticket-hub-cli migrate` already fully satisfies D111 (upgrades) -- no new command needed.
- New `IDatabase::backup`/`restore` in both adapters; new SQLite-integration test coverage.
- Verified end-to-end on both SQLite and a live PostgreSQL server, matching Phase 7's exit gate exactly:
  seed → backup → destroy → restore, with data and an attachment file round-tripping correctly.

## Unreleased — Numbered/offset pagination for issues (D126) — Phase 6 complete

- **Pagination**: `GET /api/v1/issues` now accepts optional `page` (1-based, default 1) and `pageSize`
  (default/max 200, D125) query parameters. The response gains `page`/`pageSize`/`totalItems`/
  `totalPages` fields alongside the existing `items` array. A caller sending neither parameter gets
  exactly the same result set as before.
- **Bug fix**: the route's underlying query had always silently capped results at 200 rows with no way
  for a caller to detect truncation. `totalItems` now makes this visible, and `page`/`pageSize` let a
  caller page past it.
- No other list endpoint is paginated yet -- this is a deliberate partial rollout of D126, documented as
  still open for every other list endpoint.
- New `Domain::Page<T>`, `IDatabase::listIssues(filter, limit, offset)`, `IDatabase::countIssues(filter)`
  in both database adapters, and `TicketService::listIssuesPaged`. The existing unpaginated
  `listIssues(filter)` is untouched (still used internally and by CSV export).
- This closes out Phase 6's entire roadmap list.

## Unreleased — Security hardening pass — Phase 6 continued

- **Security fix**: an uploaded attachment's `Content-Type` is caller-supplied and unvalidated (D98 has
  no upload-time MIME allow-list). A file uploaded with a spoofed `text/html` Content-Type could
  previously execute an embedded `<script>` payload same-origin, either via direct download-URL
  navigation (served as `Content-Disposition: inline`) or via the app's own text/PDF preview (rendered in
  an unsandboxed `<iframe>`) -- a real stored-XSS/CSRF-bypass chain reachable by any project member
  against any other user who previewed the attachment. Fixed with two independent layers: the preview
  `<iframe>`s now carry `sandbox=""`, and the download route now serves `Content-Disposition: attachment`
  (forced download, not rendered) for any content type that could execute script as a document
  (text/html, xhtml, svg, xml, javascript variants), leaving normal image/audio/video/PDF/plain-text
  previews unaffected.
- **Security headers**: `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, and
  `Referrer-Policy: same-origin` on every response; a `Content-Security-Policy` (`script-src 'self'`,
  among other directives) on the HTML document.
- Dependency review: Crow is pinned to release tag `v1.3.3`, not a floating branch (no change needed).
- Session/CSRF review: confirmed no regressions to cookie flags (`HttpOnly`/`Secure`/`SameSite=Strict`)
  or CSRF enforcement from earlier Phase 6 batches.

## Unreleased — Fixed request/batch-size constants (D125) — Phase 6 continued

- **Max request body size**: every JSON request body (20 call sites) is now capped at 1 MiB, returning
  `413` before parsing if exceeded. The multipart attachment upload route is unaffected (it already has
  its own stricter 25MB/file limit, D98).
- **Max bulk items**: every `POST /api/v1/issues/bulk/*` route's `issueKeys` array is now capped at 200
  items, returning `400` if exceeded.
- Max page size not implemented -- has no meaning until numbered/offset pagination (D126) exists.
- No admin configuration for either limit, per D125.

## Unreleased — Read-only CSV export of issues (D48) — Phase 6 continued

- **CSV export**: `GET /api/v1/issues/export.csv` returns the same issues `GET /api/v1/issues` would,
  as CSV, with the same query-parameter filters (`project`, `status`, `type`, `priority`, `assignee`,
  `label`, `dueBefore`, `q`) and authorization.
- Columns: key, project, summary, description, type, status, priority, reporter, assignee, storyPoints,
  dueDate, resolution, labels (semicolon-joined), createdAt, updatedAt. RFC 4180-style field escaping.
- No CSV import, no Jira migration tool -- out of scope per D48.
- `web/`: the Issues view gained an "Export CSV" link that reflects the current filters.

## Unreleased — Versioned `/api/v1` prefix (D127) — Phase 6 continued

- **API versioning**: every route now lives under `/api/v1` (e.g. `POST /api/v1/auth/login`,
  `GET /api/v1/issues`), except `GET /api/health`, deliberately kept unversioned (common infra/
  monitoring convention; not specified by any decision text).
- Pure URL rename -- no server/domain/database logic changed. `web/app.js`'s API call sites were updated
  to match.
- Formal `/api/v2` deprecation policy remains deferred until a real v2 is needed, per D127.

## Unreleased — Fixed rate limits (D124/D125) — Phase 6 continued

- **Rate limiting**: a new in-memory `TicketHub::Web::RateLimiter` (fixed-window, no admin config)
  enforces two fixed limits, returning `429` with a `Retry-After` header (in seconds, matching the
  limiter's fixed window) on trip:
  - `POST /api/auth/login`: 20 attempts per IP per 15 minutes, complementing (not replacing) the existing
    per-account 10-attempts/15-minutes lockout.
  - Every write route (POST/PUT/PATCH/DELETE): 120 requests per minute, keyed by user id when
    authenticated, else by IP.
- Process-lifetime in-memory state only (resets on restart) -- no shared cache/job infrastructure exists
  in V1.
- No new migration; no database changes.
- New standalone `ticket-hub-ratelimiter-tests` binary (no Crow dependency), passing in every build
  configuration including SQLite-only and PostgreSQL-only.
- No web UI change -- a 429 surfaces through the existing generic API-error handling.

## Unreleased — Active-session list and "sign out everywhere" (D54) — Phase 6 continued

- **Active sessions**: `GET /api/sessions` lists the caller's own active web sessions;
  `POST /api/sessions/sign-out-others` signs out every other session for the caller, keeping the current
  one active (session-cookie-only, not usable via PAT).
- No new migration -- `sessions` already had everything needed.
- No web UI yet for viewing/signing out sessions.

## Unreleased — Personal access tokens (D39/D40) — Phase 6 started

- **Personal access tokens**: self-service create/list/revoke via `POST`/`GET /api/tokens` and
  `DELETE /api/tokens/{id}` (session-cookie-authenticated). The raw token is shown only once, at
  creation. Hashed storage, a mandatory expiration, revocation, and last-used tracking (D40) -- no
  scopes (a token carries exactly its owner's permissions), no rotation, no admin-configurable lifetime.
- `Authorization: Bearer <token>` now authenticates any existing route, alongside the existing session
  cookie (mutually exclusive per request). Requests authenticated via Bearer token are exempt from the
  CSRF check (which only defends against cookie-based forgery).
- New migration `015_personal_access_tokens.sql` (both backends).
- No web UI yet for managing tokens.

## Unreleased — Attachments (D15/D98-D105) — Phase 5 complete

- **Attachments**: issues can now have files attached, closing out Phase 5 (and Milestone 2).
  - Local filesystem storage only, hardwired (D15) -- no S3/pluggable backend, no storage abstraction.
  - Fixed limits (D98): 25MB/file, 20 attachments/issue, a blocked-extension denylist. No admin
    configuration, no quotas, no antivirus/DLP.
  - All four native-element previews (D99): image (`<img>`), PDF and text (`<iframe>`), audio/video
    (`<audio>`/`<video>`). No PDF.js or other heavy libraries.
  - Full upload + drag/drop + paste support in the Markdown editor (D100), referencing
    `attachment://<id>`.
  - Sortable list (name/size/date/uploader/type) + recycle bin (D101), fixed 90-day on-demand retention
    (D102).
  - Upload-time SHA-256/size verification only (D105) -- no periodic integrity audit.
- New `GET`/`POST /api/issues/{key}/attachments`, `DELETE /api/issues/{key}/attachments/{id}`,
  `GET /api/attachments/{id}/download`, and the recycle-bin routes
  (`GET /api/attachments/deleted`, `POST /api/attachments/{id}/restore`,
  `DELETE /api/attachments/{id}/permanent`).
- `web/`: the issue drawer gained an Attachments section (sort, upload, drag-and-drop, inline previews,
  delete); the Markdown toolbar gained an attach button plus real drag-and-drop/paste; a new admin-only
  "Attachment recycle bin" nav item.

## Unreleased — Kanban board WIP limits (D32/D33) — Phase 5 continued

- **Kanban board WIP limits (D32/D33)**: `GET /api/board-columns` returns one entry per fixed workflow
  status with its optional `wipLimit`; `PUT /api/board-columns/{statusKey}` sets or clears it
  (global-administrator-only). This is a single flat, installation-wide setting -- one limit per status
  shared by every project's board, not a per-project setting.
- New migration `013_board_columns.sql` (both backends).
- `web/`: the Board view shows each column's count as `N / limit` with a soft, non-blocking highlight
  when a column is over its limit; global admins get an inline editor to set/clear the limit.

## Unreleased — Personal dashboard widgets (D24) — Phase 5 continued

- **Personal dashboard (D24)**: `GET /api/dashboard` now also returns `assignedToMe`, `watchedIssues`, and
  `upcomingDeadlines` for an authenticated caller (empty for an anonymous viewer). Matches D24's fixed
  personal-dashboard widget set (assigned issues, watched issues, recent activity, deadlines, simple
  stats), minus the active-sprint widget dropped by D24 itself since Scrum was removed for V1.
  `assignedToMe`/`upcomingDeadlines` exclude Done-category issues; `upcomingDeadlines` is sorted soonest
  due date first.
- New `IDatabase::listWatchedIssues(userId, limit)` in both adapters.
- `web/`: the Dashboard view gained "Assigned to me", "Issues I'm watching", and "Upcoming deadlines"
  panels.

## Unreleased — Ad-hoc issue filter/search widening (D10/D43) — Phase 5 started

- **Filter widening (D10/D43)**: `GET /api/issues` now also accepts `type`, `priority`, `assignee`,
  `label`, and `dueBefore` query parameters, alongside the existing `project`/`status`/`q`. Still the
  ad-hoc, in-UI-only filter model (no saved/shared filters, no JQL, not usable as a webhook/board source).
  `q` now also matches issue description, not just summary/key (plain `LIKE`/`ILIKE` substring match, no
  full-text index).
- `web/`: the Issues view filter bar gained type/priority/assignee dropdowns, a label input, and a
  due-date picker.
- This is the first Phase 5 (Attachments and Kanban board) slice. Dashboard personalization (D24), Kanban
  WIP limits/drag-and-drop (D33), and the attachments vertical (D15/D98-D105) remain.

## Unreleased — Simple append-only admin/security audit log (D23) — Phase 4 complete

- **Audit log (D23)**: a simple, append-only `audit_events` table -- no categories-as-a-feature, export, or
  configurable retention; rows are never purged. Recorded automatically as a side effect of a small,
  focused set of admin/security-relevant actions: `auth`/`login.failed`, `auth`/`login.blocked` (a login
  attempt against an already-locked account), `identity`/`user.created` (via `ticket-hub-cli create-user`,
  which has no actor since it runs outside any web session), `admin`/`settings.anonymous_read_changed`,
  `admin`/`project.permanently_deleted`, and `admin`/`issue.permanently_deleted`.
- New `GET /api/admin/audit-events` route, global-administrator-only, returning the newest 200 events.
- `web/`: a new "Audit log" nav item, visible only to global admins, showing a simple read-only table
  (when, category, action, actor, target, details).
- This closes out Phase 4 (Collaboration) -- every item in `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 4 list
  is now implemented. Phase 5 (Attachments and Kanban board) is next.

## Unreleased — Simplified worklogs (D12/D13)

- **Simplified worklogs (D12/D13)**: log time spent on an issue via `POST /api/issues/{key}/worklogs`
  (`workDate`, `timeSpentSeconds`, optional `comment`). No remaining-estimate linkage (D12 dropped time
  estimates from V1 entirely) and no own-vs-others edit/delete permission split (D13): any project member
  with issue access (project-Member-or-above, the same level as any other issue write) may edit or delete
  *any* worklog on that issue, not just the one they logged themselves -- deliberately more permissive
  than comments' author-or-admin rule (D83).
- New migration `011_worklogs.sql` (both backends): `worklogs` table with a tombstone delete
  (`deleted_at`/`deleted_by_user_id`, mirroring comments/issues/projects) and the same optimistic-locking
  contract as comment/issue edits (`expectedVersion` -> `Domain::ConcurrencyConflict`, 409).
- New `GET`/`POST /api/issues/{key}/worklogs` and `PATCH`/`DELETE /api/issues/{key}/worklogs/{id}` routes.
- `web/`: a "Time tracking" section in the issue drawer listing logged time (duration, author, date,
  optional comment) with a delete button on every entry (any project member, not just the author -- no
  client-side author check, matching the server's more permissive D13 rule), and a log-time form
  (date, a free-text duration like "1h 30m", optional comment).

## Unreleased — Markdown editor toolbar, live preview, and sanitized rendering (D16)

- **Rendered Markdown (D16)**: comment bodies and issue descriptions now render as formatted HTML
  (bold/italic/inline code/links/headings/lists/blockquotes/fenced code/horizontal rules) instead of
  plain escaped text. The renderer is a deliberately small subset, not a general-purpose Markdown
  engine, and is safe by construction: the raw input is HTML-escaped *first*, and every transform only
  ever wraps the already-escaped text in a fixed set of hardcoded tags, so user input can never
  introduce a real HTML tag or attribute -- there is no separate sanitization pass to get wrong. Link
  URLs are restricted to `http(s)`/`mailto`; any other scheme (e.g. `javascript:`) is left as literal
  `[text](url)` text instead of becoming a clickable link.
- **Visual toolbar and live preview**: Bold/Italic/Code/Link/Bulleted-list/Numbered-list/Quote buttons
  above every Markdown-capable textarea (comment add, comment edit, issue description on create and
  edit), plus a Preview toggle that swaps the textarea for a live-rendered view of its current content.
- Bold/italic use only `**`/`*`, not `__`/`_` -- underscore delimiters are ambiguous with snake_case/
  dunder identifiers (e.g. `__init__`), where a naive regex would treat two adjacent underscores
  elsewhere in the text as an unintended emphasis span.
- No schema or API change: comment/description bodies are still stored and transmitted as raw Markdown
  text: this is a display-only change, plus the toolbar/preview UI.

## Unreleased — @mention handles and the fixed in-app notification set (D56/D80/D14)

- **@mention handles (D56)**: `users.handle` (migration `010_mentions_and_notifications.sql`, both
  backends) -- optional, unique (case-insensitive via lowercase normalization), 1-32
  letters/digits/underscores. Set at account creation via `ticket-hub-cli create-user ... --handle=<handle>`;
  there is no self-service profile editing yet. The three seeded demo accounts now have handles
  (`demo`, `alex`, `sam`).
- **@mentions (D80)**: a comment body's `@handle` tokens are parsed once, at creation time (not on every
  edit), and resolved against the handle directory; each resolved user (other than the comment's own
  author) gets a "mentioned" notification. `GET /api/users` backs the new @mention autocomplete dropdown
  in the comment textarea (both add and edit).
- **Fixed in-app notification set (D14)**: exactly three types -- `assigned` (a new or changed assignee,
  skipping self-assignment and unchanged re-saves), `mentioned` (D80), and `watched_comment` (a new
  comment on an issue you watch, skipping the comment's own author). No email, no configurable schemes,
  no per-user preferences or digests. A recipient who is both mentioned and a watcher on the same comment
  gets exactly one notification (mentioned wins), not two. New `notifications` table (migration
  `010_mentions_and_notifications.sql`) and `GET /api/notifications[?unread=true]`,
  `GET /api/notifications/unread-count`, `POST /api/notifications/{id}/read`,
  `POST /api/notifications/read-all` routes.
- `web/`: a notification bell in the top bar with an unread-count badge, opening a dropdown list;
  clicking a notification marks it read and opens the related issue. Browser-verified end-to-end:
  assigning an issue notifies the assignee, mentioning a user in a comment notifies them (with working
  autocomplete), and the notification panel/badge/mark-read/mark-all-read flow all work through the real
  HTTP layer.

## Unreleased — Fixed emoji reactions on comments (D84)

- **Fixed emoji reactions (D84)**: comments can now receive reactions via
  `POST`/`DELETE /api/issues/{key}/comments/{id}/reactions/{key}`, where `{key}` is one of a fixed
  eight-value catalog (`thumbs_up`, `thumbs_down`, `laugh`, `hooray`, `confused`, `heart`, `rocket`,
  `eyes` -- GitHub's well-known reaction set, used as a conservative default since the decision register
  calls for "a fixed reaction set" without naming one). Migration `009_comment_reactions.sql` adds
  `comment_reactions` (both backends), a three-column composite-key many-to-many table mirroring
  `issue_watchers`/`issue_votes`.
- Self-service, no project-role check -- same reasoning as watch/vote (D20/D79): any authenticated user
  may react to any comment. Idempotent: reacting (or un-reacting) twice with the same key is a no-op.
  `GET /api/issues/{key}/comments/{id}/reactions` returns the raw `(reactionKey, user)` list; the client
  groups it into per-reaction counts and highlights the viewer's own reactions.
- `web/`: each comment shows all eight reactions as small pill buttons with a live count, highlighting the
  ones the current viewer has added; clicking toggles react/un-react. Browser-verified across two users,
  confirming counts are shared while each user's own "active" highlight is independent.
- New SQLite-integration, authorization-integration, and live-PostgreSQL coverage for
  `addCommentReaction`/`removeCommentReaction`/`listCommentReactions`.

## Unreleased — Comment editing and tombstone delete (D81/D82/D83): Phase 4 started

- **Comment editing (D81)**: comments can now be edited via `PATCH /api/issues/{key}/comments/{id}`, with
  the same optimistic-locking contract as issue edits (`expectedVersion` -> `Domain::ConcurrencyConflict`,
  409) and a single `edited_at` timestamp recorded instead of a full version-history table. Migration
  `008_comment_editing.sql` adds `comments.edited_at` on both backends.
- **Tombstone delete (D82)**: `DELETE /api/issues/{key}/comments/{id}` soft-deletes via the same
  `deleted_at`/`deleted_by_user_id` columns issues and projects already use. The comment row and its
  original body remain in the database, excluded from ordinary listing -- there is no separate admin
  recycle-bin API for comments, unlike issues and projects.
- **Simplified permissions (D83)**: the comment's own author may always edit/delete it; otherwise the
  actor needs project-Admin-or-above on the comment's issue's project (or global admin) -- no separate
  edit-own/edit-all/delete-own/delete-all matrix.
- `web/`: comments gained Edit/Delete buttons (hidden client-side for non-author/non-global-admin
  actors), an inline edit textarea with Save/Cancel, and an `(edited)` marker. Browser-verified with
  Playwright/Chromium, including a cross-user check that a non-author, non-admin user cannot see
  Edit/Delete on someone else's comment.
- New SQLite-integration and authorization-integration test coverage for `findCommentById`/
  `editComment`/`deleteComment`.

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
