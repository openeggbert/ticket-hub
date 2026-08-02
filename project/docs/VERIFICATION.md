# Verification record

## 2026-08-02 — Light and dark theme (D46): Phase 8 slice 2

CSS-only batch, no C++/schema/route changes. Still open in Phase 8: the baseline accessibility review
(D47), the browser-support note (D139, already satisfied by construction), the threat-model/security
self-review, and a final documentation-currency pass.

### What changed

- `web/styles.css`'s `:root` gained a set of new CSS custom properties that were previously missing
  (`--surface-hover`, `--text-secondary`, `--info-bg`/`--info-text`, `--success-bg`/`--success-text`,
  `--danger-bg`/`--danger-border`, `--warning-bg`/`--warning-border`/`--warning-text`, `--overlay`,
  `--topbar-bg`) and `color-scheme: light dark;`, so native form controls (checkboxes, date pickers, the
  default focus ring) follow the OS dark-mode signal automatically even before any custom override is
  applied.
- A new `@media (prefers-color-scheme: dark) { :root { ... } }` block redefines every theme variable with
  a dark-appropriate value (`--bg: #12151c`, `--surface: #1c212c`, `--text: #e4e8f1`, `--primary:
  #579dff`, etc.) -- D46 calls for "light and dark theme... simple", with no high-contrast/branding
  requirement and no mention of a manual in-app toggle, so this deliberately follows the OS-level
  `prefers-color-scheme` signal only; there is no persisted user preference or UI switch, since building
  one would be new state/UI machinery the decision text does not call for.
- Roughly 30 hardcoded literal colors scattered through the stylesheet were converted to reference the
  variables above instead (`background: white` -> `var(--surface)` across ~15 rules; `color: #44546f` ->
  `var(--text-secondary)` across ~8 rules; plus targeted fixes for status/priority/label chips, unread
  notifications, the bulk-action bar, form/error banners, the update banner, modal/drawer backdrops, the
  filter bar, board columns, issue-table hover/border colors, the over-WIP-limit column count, ghost
  buttons, small avatars, the loading spinner, and the inline WIP-limit editor). Left deliberately
  unconverted: the sidebar (a fixed navy `#0b214a`, always dark by design regardless of theme), `.avatar`/
  `.project-avatar` (fixed colored badges with their own contrasting fixed text), and `.toast` (a fixed
  dark overlay) -- all three are self-contained and stay readable in both themes without needing to track
  the page theme.
- Found and fixed a real bug during verification: `.link-form input` (the "Issue key, e.g. TH-3" field in
  the issue-linking UI) had no explicit `background`/`color` at all, so in dark mode it fell back to the
  browser's light default form-control appearance -- a white box that was hard to read against the rest of
  the now-dark page. Fixed by adding explicit `background: var(--surface); color: var(--text);`, matching
  every other form input in the stylesheet.

### Verification

Ran a Playwright/Chromium script (`dark_theme_test.mjs`, scratchpad-only, not committed) that logs in and
emulates both `colorScheme: 'light'` and `colorScheme: 'dark'` at the browser level:

- Confirmed light mode is byte-identical to before this change (computed `body` background, `.content`
  text color, and `.panel` background all matched the pre-existing light-theme values).
- Confirmed dark mode correctly switches: computed `body` background resolved to `rgb(18, 21, 28)`,
  matching the new `--bg: #12151c` variable, and the issue drawer's background switched accordingly too.
- Took screenshots of five views in dark mode -- the Issues list, the issue drawer, the create-issue
  modal, the Kanban board, and the Projects grid -- and reviewed each image directly, confirming all text/
  chip/border contrast is readable and no element is left with a stray light-mode background. This is how
  the `.link-form input` bug above was actually caught: a screenshot of the issue drawer with the "Link
  issue" panel open showed a plainly-wrong white input box against the dark surface.
- Re-ran the existing `login_browser_test.mjs` and `reorder_move_bulk_test.mjs` Playwright regression
  scripts unchanged -- both passed, confirming this CSS-only change caused no functional regression.
- `ctest --output-on-failure`: 8/8 green (unaffected by construction -- no C++ source changed).

Not independently verified: automated contrast-ratio (WCAG) measurement -- deferred to the upcoming
accessibility baseline review (D47), which is the next item in Phase 8.

## 2026-08-02 — Docker image and Compose distribution (D50): Phase 8 slice 1

First Phase 8 (Milestone 4) slice, directly following the close of Phase 7/Milestone 3. Still open in
Phase 8: light/dark theme (D46, currently light-only), the baseline accessibility review (D47), the
browser-support note (D139, already satisfied by construction -- modern vanilla JS, no polyfills), the
threat-model/security self-review, and a final documentation-currency pass.

### What changed

- New two-stage `Dockerfile`. `build` stage: `debian:bookworm-slim` plus the exact build dependencies
  README.md's own "Building" section already documents (`build-essential cmake libpq-dev libsqlite3-dev
  libargon2-dev libasio-dev`, plus `git`/`ca-certificates` for Crow's network-fetched CMake
  `FetchContent`), configures with `-DTICKETHUB_BUILD_SERVER=ON` (which also builds the CLI, since
  `TICKETHUB_BUILD_CLI` defaults `ON`), and runs `cmake --install build --prefix /opt/ticket-hub`.
  `runtime` stage: the same base image with only the shared libraries needed at runtime (`libpq5
  libsqlite3-0 libargon2-1`) plus `curl` (for the `HEALTHCHECK` below), a non-root `ticket-hub` system
  user, and `COPY --from=build /opt/ticket-hub /opt/ticket-hub`.
- `src/config/Config.cpp`'s compiled-in defaults for `TICKETHUB_WEB_ROOT`/`TICKETHUB_MIGRATIONS_ROOT`
  point at the *build machine's* source checkout (`TICKETHUB_SOURCE_DIR`, a compile-time macro) -- this
  does not exist in the runtime image, so the Dockerfile sets both explicitly via `ENV` to the installed
  `share/ticket-hub/...` paths (README.md already flags this exact gap: "installed deployments should set
  `TICKETHUB_MIGRATIONS_ROOT`..."). `TICKETHUB_ATTACHMENTS_DIR` is similarly set to `/data/attachments`
  (a dedicated `VOLUME`), since its default is also cwd-relative.
- `HEALTHCHECK` runs `curl -fsS http://127.0.0.1:$TICKETHUB_PORT/api/health` -- a real end-to-end check
  that the HTTP server is actually accepting connections and can talk to its database, not just that the
  process exists.
- New `.dockerignore` (`build/`, `build-*/`, `.git/`, `*.db`/`*.sqlite3`, `data/`) to keep the build
  context small and avoid ever accidentally baking a local SQLite file or build artifacts into an image
  layer.
- `docker-compose.yml` gained a `ticket-hub` service: `build: .` (the new Dockerfile), `depends_on:
  postgres: condition: service_healthy` (waits for Postgres's existing `pg_isready` healthcheck before
  starting), `TICKETHUB_DATABASE_URL` pointed at the `postgres` service by its Compose-internal DNS name
  (not `127.0.0.1`, since from inside the `ticket-hub` container the database is a separate container),
  `TICKETHUB_SEED_DEMO: "false"` by default (a real deployment should not ship pre-seeded demo accounts;
  documented alongside how to create the first real admin or flip the flag for demo data), a
  `ticket-hub-attachments` named volume mounted at `/data`, and its own `HEALTHCHECK`. The existing
  `postgres` service (and its host-published port 5432) is otherwise unchanged, preserving the
  `docker compose up -d postgres`-only workflow README.md's "Run with PostgreSQL" section already
  documents (build from source, run only the database in Docker).

### Verification -- and an explicit disclosure of what could not be tested here

This environment's outbound network access goes through a policy-enforcing agent proxy
(`/root/.ccr/README.md`). Attempting `docker compose build ticket-hub` got past Dockerfile parsing and
manifest resolution for `debian:bookworm-slim` (proving DNS/registry-auth reachability through the proxy
works), but failed pulling the actual image layer blobs with `403 Forbidden` from
`production.cloudfront.docker.com` -- the CDN host Docker Hub's registry redirects layer downloads to.
Confirmed via `curl -sS http://127.0.0.1:39727/__agentproxy/status` that this request *did* reach the
proxy (`recentRelayFailures` shows `connect_rejected` / `gateway answered 403 to CONNECT (policy denial
or upstream failure)` for that exact host, twice, matching two build attempts) -- this is a genuine
organization egress-policy denial on that specific CDN host, not a proxy misconfiguration, a TLS/CA
problem, or a bug in the Dockerfile. Per this environment's own explicit operating rule ("do not retry
organization policy denials (403/407) — report them instead"), this was not retried or routed around
(e.g. no attempt to switch base images, mirror registries, or disable the proxy). This is a report-only
environmental limitation of this specific development sandbox, not a defect in what was written.

Given that constraint, verification focused on everything that *could* be checked without an actual image
build:

1. `docker compose config`: the full merged Compose configuration (both services, healthchecks, the new
   volume, environment variables) parses and resolves cleanly -- confirms YAML/schema correctness.
2. `docker build --check .`: Docker's built-in Dockerfile linter reports "Check complete, no warnings
   found" -- confirms Dockerfile syntax and best-practice conformance (this check runs entirely locally,
   without pulling any image).
3. **Runtime-configuration equivalence test**, run directly on the host: `cmake --install build --prefix
   <dir>` was run standalone and its output tree inspected -- confirmed it produces exactly
   `bin/ticket-hub`, `bin/ticket-hub-cli`, `share/ticket-hub/web/...`, and
   `share/ticket-hub/migrations/{sqlite,postgresql}/...`, i.e. exactly the paths the Dockerfile's `ENV`
   lines assume. The installed `bin/ticket-hub` binary was then run twice with the *exact* environment
   variables the Dockerfile/Compose file set (`TICKETHUB_WEB_ROOT`/`TICKETHUB_MIGRATIONS_ROOT` pointed at
   the installed tree, `TICKETHUB_ATTACHMENTS_DIR` pointed at a fresh directory simulating the `/data`
   volume): once against a fresh SQLite file (confirmed `/api/health` responds, `/` serves the installed
   `index.html`, and the attachments directory is created at the configured path), and once against a
   **live PostgreSQL database** using the *exact* connection-string shape `docker-compose.yml` uses
   (`host=... port=5432 dbname=tickethub user=tickethub password=tickethub-dev`, differing only in
   `host=127.0.0.1` vs `host=postgres` -- purely a DNS-name difference Docker's internal networking
   resolves automatically between Compose services) -- confirmed `/api/health` responds correctly there
   too. Also ran `bin/ticket-hub-cli create-user ... --admin` against the installed binary with the same
   environment, confirming the exact `docker compose exec ticket-hub ticket-hub-cli create-user ...`
   workflow documented in README.md's new "Run with Docker" section actually works end-to-end.
4. `ctest --output-on-failure`: 8/8 green -- this batch added no new C++ source files and changed no
   application code, only packaging files, so the existing suite is unaffected by construction; still run
   to confirm nothing else regressed.

Taken together, this is strong indirect evidence the Dockerfile/Compose file are correct and would build
and run successfully in any environment with ordinary Docker Hub network access -- but it is explicitly
**not** the same as having actually run `docker compose up` end-to-end in a container, and this record
says so precisely rather than implying otherwise.

## 2026-08-02 — Structured JSON logs and admin version banner (D112/D133): Phase 7 slice 2, Phase 7 complete

Second and final Phase 7 slice. Every item in `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 7 list is now
implemented: backup/restore (D106-D108), the upgrade mechanism (D111, already satisfied by
`ticket-hub-cli migrate`), structured JSON logs to stdout (D133), and the in-app admin version banner
(D112). **This closes Phase 7.**

### What changed -- structured JSON logs (D133)

- New `TicketHub::Web::JsonLogHandler` (`src/web/JsonLogHandler.h/.cpp`), implementing Crow's
  `ILogHandler` interface. `log(message, level)` writes one `crow::json::wvalue` object per call to
  `std::cout` with fields `timestamp` (ISO 8601 UTC, matching the format already used for
  `audit_events`/every migration's `created_at`), `level` (lowercased), `service` (always `"ticket-hub"`),
  and `message` (Crow's own log message text, correctly JSON-escaped by reusing `crow::json::wvalue`'s
  own serializer rather than hand-rolling escaping).
- Registered once in `HttpServer.cpp`'s `runHttpServer`, via `crow::logger::setHandler(&jsonLogHandler)`
  (a `static` local, since Crow's logger keeps a raw non-owning pointer that must outlive `app.run()` --
  matching how Crow's own default handler is also a function-local static internally). This replaces
  Crow's default `CerrLogHandler` (plain-text lines to stderr) globally, so every log call Crow already
  makes internally -- server startup, per-request Info-level request/response lines, warnings/errors --
  becomes structured JSON on stdout with zero new call sites needed anywhere else in the app.
- The one manual `std::cout <<` startup banner line in `HttpServer.cpp` was switched to `CROW_LOG_INFO`
  so it also flows through the same handler instead of bypassing it as raw unstructured text.
- New source file added to the `ticket-hub` server target only in `CMakeLists.txt` (it depends on
  `crow.h`, same as `RateLimiter`/`Api`/`HttpServer`).

### What changed -- admin version banner (D112)

- D112's V1 answer: "Simple in-app admin banner when a newer version is available; no email delivery."
  No decision text (or any other document in this repository) specifies *how* the app would discover the
  latest available version -- there is no outbound-HTTP-client library linked anywhere in this codebase,
  and no URL/manifest/release-feed is named. Building an actual "check GitHub releases" (or similar)
  mechanism would be genuinely new capability, not implementing an already-decided detail. The
  conservative, explicitly documented choice: a global-administrator-only setting the admin fills in
  themselves (e.g. after checking a release page), compared against the running build's own
  `TICKETHUB_VERSION`. No outbound network calls, consistent with the rest of V1's offline-friendly
  self-hosted posture.
- New `TicketService::latestKnownVersion(actor)`/`setLatestKnownVersion(version, actor)`, both
  `requireGlobalAdmin`-gated (unlike the anonymous-read-access toggle, whose *read* is available to any
  session -- here, only an admin ever acts on this value, so there is no reason for a non-admin to see
  it). Reuses the existing generic `installation_settings` key/value table via `getSetting`/`setSetting`
  (no new migration -- `latest_known_version` is just a new key in an already-existing table) and the
  existing `recordAuditEvent` call, exactly mirroring the anonymous-read-access toggle's implementation
  pattern.
- New `GET`/`PUT /api/v1/settings/latest-known-version` routes in `Api.cpp`, structurally identical to
  the existing `GET`/`PUT /api/v1/settings/anonymous-read` routes (CSRF/rate-limit/body-size checks on
  the write side, `Domain::Forbidden` → 403 mapping). `GET` returns `{currentVersion, latestKnownVersion,
  updateAvailable}` -- `updateAvailable` is a simple string-inequality comparison against the setting
  when present, `false` when the setting has never been set. `PUT` rejects an empty/missing `version`
  with `400`.
- `web/index.html` gained a `#update-banner` element (hidden by default) between the topbar and the main
  content area. `web/app.js`'s `loadBaseData()` (called on every page load and after login) now also
  calls a new `refreshUpdateBanner()`: skips entirely for a non-admin (`state.principal.isAdmin` false,
  matching the route's own 403 rather than surfacing an error toast for something a non-admin can't act
  on anyway), otherwise fetches the endpoint and shows/hides the banner based on `updateAvailable`.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 8/8 green -- unchanged; both features are HTTP/logging-layer additions
   with no domain/database behavior change to any existing tested path (the `installation_settings` key
   reuses already-tested generic `getSetting`/`setSetting` methods).
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile cleanly and pass their
   respective test suites (8/8 and 4/4). `JsonLogHandler.cpp` is server-target-only (not part of
   `ticket-hub-core`), so it is not compiled in either config, matching `RateLimiter`'s precedent.
4. No live-PostgreSQL check for this batch specifically: the version-banner feature reuses the already
   live-PostgreSQL-verified generic `getSetting`/`setSetting` code path (previously exercised by the
   anonymous-read-access toggle in an earlier phase) with no new SQL in either adapter; the JSON-log
   change has no database dimension at all.
5. End-to-end HTTP verification via `curl` against a locally running server (SQLite, demo-seeded):
   - Piped real request traffic (`/api/health`, a failed login) through the running server and confirmed
     every stdout line parses as valid JSON (Python's `json.loads`) with the four expected fields present.
   - `GET /api/v1/settings/latest-known-version` before ever setting it: `latestKnownVersion: null`,
     `updateAvailable: false`.
   - `PUT` with the same version as `TICKETHUB_VERSION`: `updateAvailable` stays `false`. `PUT` with a
     different (higher) version string: `updateAvailable` becomes `true`.
   - `PUT` with an empty `version`: `400`.
   - The same `GET` as a non-admin (`sam`): `403`.
6. Browser verification via Playwright/Chromium: logged in as the admin (`demo`) after setting
   `latest_known_version` to `0.3.0` via `curl`, confirmed the banner is visible with the exact expected
   text (`"A newer Ticket Hub version is available: 0.3.0 (currently running 0.2.0)..."`); logged in as
   the non-admin (`sam`) against the same server state and confirmed the banner is not visible at all.
   Also re-ran the existing `project_management_test.mjs` regression script (unrelated flow, exercising a
   different part of `loadBaseData()`'s callers) to confirm no unrelated regression from touching
   `loadBaseData()`.

## 2026-08-02 — Backup and restore (D106-D108): Phase 7 slice 1

First Phase 7 slice, directly following the close of Phase 6. Still open in Phase 7: the in-app admin
banner for available updates (D112) and structured JSON logs to stdout (D133). D111 (upgrades) required
no new work -- `ticket-hub-cli migrate` already fully satisfies it.

### What changed

- New `IDatabase::backup(directory)`/`restore(directory)` (pure virtual, implemented in both adapters):
  scoped to the database only -- the CLI commands separately handle copying the attachments directory,
  since that is not a database concern.
- **SqliteDatabase**: uses the SQLite online backup API (`sqlite3_backup_init`/`sqlite3_backup_step`/
  `sqlite3_backup_finish`) rather than a raw file copy. The database runs in WAL mode
  (`PRAGMA journal_mode = WAL`, set in the constructor); a plain `cp`/`std::filesystem::copy_file` of
  only the main `.sqlite3` file could silently miss data still sitting in an unmerged `-wal` file. The
  backup API produces a correct, complete snapshot regardless of WAL/checkpoint state. `restore` runs the
  same API in reverse: the backup file (opened read-only) is the *source*, and the currently-open
  `database_` connection is the *destination*, so restoring replaces the live database's content in
  place through the same handle every other method already uses.
- **PostgresDatabase**: shells out to the `pg_dump`/`psql` client binaries (both standard PostgreSQL
  tools, not reimplemented over libpq -- re-deriving dump/restore logic would be substantial, fragile
  duplication for no benefit). `backup` runs `pg_dump --clean --if-exists <conninfo> -f
  <directory>/database.sql` -- `--clean --if-exists` makes the dump self-contained for a *direct* restore
  (D108: no isolated staging environment): replaying it against a non-empty target works, since the dump
  itself drops each object (if it exists) before recreating it. `restore` runs
  `psql -v ON_ERROR_STOP=1 <conninfo> -f <directory>/database.sql` -- `ON_ERROR_STOP=1` makes psql abort
  (non-zero exit, caught and turned into a thrown exception) on the first SQL error instead of continuing
  and reporting overall success on a partially-failed restore. A local `shellQuote` helper single-quotes
  the connection string and file paths before interpolating them into the shell command string; both are
  admin-controlled (from `TICKETHUB_DATABASE_URL` and a CLI argument), not untrusted network input, but
  quoting is applied regardless for correctness against paths/values containing shell metacharacters or
  spaces.
- New `ticket-hub-cli backup <output-directory>`: copies `config.attachmentsRoot` into
  `<output-directory>/attachments` (if it exists), then calls `database->backup(...)`. Refuses to write
  into a directory that already exists and is non-empty (a cheap guard against accidentally clobbering an
  existing backup or writing into an unrelated populated directory).
- New `ticket-hub-cli restore <backup-directory> --yes`: requires the literal `--yes` flag; without it,
  prints a clear warning naming the backup directory and exits `2` without touching anything. With
  `--yes`: calls `database->restore(...)`, then `database->migrate()` as an explicit, separate, visible
  step (D109: "forward migrate older supported backups" -- so a backup taken on an older schema version
  is brought current automatically as part of restoring it), then copies `<backup-directory>/attachments`
  back over `config.attachmentsRoot` (creating it if needed, overwriting existing files).
- New CLI usage text for both commands in `printUsage`.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 8/8 green, including new `ticket-hub-sqlite-integration-tests` coverage:
   `backup` writes a non-empty `database.sqlite3` into the output directory; a database mutated (one more
   issue created) *after* the backup was taken is reverted by `restore` back to exactly the 8-issue state
   the backup captured, and the post-backup issue does not survive the restore.
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile cleanly and pass their
   respective test suites (8/8 and 4/4).
4. **Live PostgreSQL, full exit-gate cycle**: found the same local PostgreSQL 16 cluster used in the
   pagination batch (`pg_lsclusters`/`service postgresql start`), created a throwaway database/role, and
   ran the exact scripted cycle Phase 7's exit gate describes:
   - `ticket-hub-cli seed-demo` (fresh install → seed).
   - Added a marker file to the attachments directory.
   - `ticket-hub-cli backup <dir>`: confirmed `<dir>/database.sql` contains `COPY` blocks with the
     expected row counts (8 issues, etc. -- `pg_dump`'s default plain-text format uses `COPY`, not
     `INSERT`, for bulk data) and `<dir>/attachments/` contains the marker file.
   - Destroyed the live database (`DROP SCHEMA public CASCADE; CREATE SCHEMA public;`) and deleted the
     attachments directory, simulating total data loss.
   - `ticket-hub-cli restore <dir>` **without** `--yes`: confirmed it printed the warning and exited `2`
     without touching anything.
   - `ticket-hub-cli restore <dir> --yes`: confirmed it succeeded (`psql`'s `COPY N` lines matched the
     original row counts exactly) and ran migrations afterward with no errors.
   - Confirmed via `psql`: issue count back to `8`, both project keys (`TH`, `WEB`) present.
   - Confirmed the marker attachment file's exact byte content survived the round trip.
   - Dropped the throwaway database/role afterward.
5. **SQLite, the same exit-gate cycle**, independently via the CLI (not just the integration-test API
   calls in step 2): seeded a fresh SQLite file, added a marker attachment file, backed up, deleted the
   database file (plus `-wal`/`-shm`) and the attachments directory entirely, confirmed restore without
   `--yes` refuses, then restored with `--yes` and confirmed (via Python's `sqlite3` module, since the
   `sqlite3` CLI binary is not installed in this environment) the issue count, project keys, and the
   marker attachment file's content all matched exactly.

## 2026-08-02 — Numbered/offset pagination for issues (D126): Phase 6 slice 8, Phase 6 complete

Eighth and final Phase 6 slice. Every item in `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 6 list is now
implemented: `/api/v1` REST surface (D127), PAT authentication (D39/D40), fixed rate limits including the
account-lockout policy (D124/D125), fixed request/batch-size constants (D125), the active-session list
(D54), read-only CSV export (D48), the security hardening pass, and now numbered/offset pagination
(D126). **This closes Phase 6 and its exit gate.** Note: this is a deliberate *partial* rollout of D126 --
only `GET /api/v1/issues` is paginated. Every other list endpoint (projects, comments, worklogs,
attachments, notifications, sessions, tokens, audit events, watchers, voters, board-columns, issue-links,
comment-reactions) remains unpaginated, documented explicitly as still open rather than silently
incomplete.

### What changed

- **Bug found while implementing this**: `SqliteDatabase::listIssues`/`PostgresDatabase::listIssues` had
  always had a hardcoded `LIMIT 200` in their SQL, with no `total` count returned anywhere and no
  documentation of the cap -- a caller with more than 200 matching issues silently got a truncated result
  set with no way to detect it. This predates this session; it was introduced in the original prototype
  and never revisited. Fixed as part of this batch (see below), not filed as a separate bug, since the
  fix *is* the pagination feature.
- New `Domain::Page<T>` template (`src/domain/Models.h`): `items`, `page`, `pageSize`, `totalItems`, and
  a computed `totalPages()`. New fixed constants `Domain::DefaultPageSize`/`Domain::MaxPageSize` (both
  200 -- deliberately equal, and deliberately matching the pre-existing 200-row cap, so a caller that
  never sends `page`/`pageSize` gets *exactly* the same result set it always got; pagination is additive
  for every existing caller, not a behavior change). This also gives concrete meaning to D125's
  previously-undefined "max page size," which had been blocked on D126 not existing yet.
- New `IDatabase::listIssues(filter, limit, offset)` and `IDatabase::countIssues(filter)` in both
  `SqliteDatabase` and `PostgresDatabase`, implemented as new methods alongside (not replacing) the
  existing unpaginated `listIssues(filter)` overload, which is still used internally
  (`TicketService`'s "assigned to me" dashboard fetch, both adapters' own "recent issues" fetch) and by
  the CSV export route (D48), which deliberately wants *everything* matching the filter, not one page of
  it. `countIssues` uses a leaner query (no label-aggregation `LEFT JOIN`/`GROUP BY`, just an `EXISTS`
  subquery for the label filter, matching how the label filter is already checked in the row-fetching
  query) to avoid the row-multiplication that the full `IssueSelect` view's label join would otherwise
  cause in a naive `COUNT(*)`.
- New `TicketService::listIssuesPaged(filter, page, pageSize, actor)`: clamps `page` to at least 1 and
  `pageSize` into `[1, MaxPageSize]` (fixed constants, no admin exceptions, per D125), computes the
  offset, and pairs one `countIssues` call with one paginated `listIssues` call.
- `GET /api/v1/issues` now calls `listIssuesPaged` instead of the unpaginated `listIssues`; new optional
  `page`/`pageSize` query parameters (new `optionalIntQueryParameter` helper, throwing `std::invalid_argument`
  -- mapped to `400` -- for a non-numeric value rather than silently ignoring it); the response envelope
  gains `page`/`pageSize`/`totalItems`/`totalPages` fields alongside the existing `items` array.
- New SQLite-integration test coverage: `countIssues` matches the unpaginated `listIssues` row count for
  the same filter; a 3-item page and a second 3-item page don't overlap and together advance past the
  first page; an offset beyond the total row count returns an empty page rather than erroring; a limit
  exceeding the total returns every matching row rather than erroring; pagination composes correctly with
  an existing filter (every row in a filtered+paginated page still matches the filter).

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 8/8 green, including the new pagination assertions in
   `ticket-hub-sqlite-integration-tests`.
3. Re-ran the SQLite-only and PostgreSQL-only build configurations (`-DTICKETHUB_BUILD_SERVER=OFF` in
   both): both compile cleanly and pass their respective test suites (8/8 and 4/4).
4. **Live PostgreSQL verification** (this batch's database-layer changes warranted it, unlike several
   recent Phase 6 batches that made no `IDatabase` changes): found a local PostgreSQL 16 cluster already
   present in this environment (`pg_lsclusters`), started it (`service postgresql start`), created a
   throwaway role/database, ran the full server against it with auto-migrate/seed-demo, and repeated
   every `curl` check below against it -- identical results to SQLite on every check. Dropped the
   throwaway database/role afterward.
5. End-to-end HTTP verification via `curl` against locally running servers (both SQLite and, per above,
   live PostgreSQL, demo-seeded):
   - No `page`/`pageSize`: `page=1`, `pageSize=200`, `totalItems=8`, `totalPages=1`, all 8 seeded issues
     returned -- confirming the exact same result set as before this batch.
   - `pageSize=3&page=1` then `pageSize=3&page=2`: six distinct, non-overlapping issue keys across the
     two pages, in stable order.
   - `pageSize=300` (exceeds `MaxPageSize`): clamped to `pageSize: 200` in the response, not rejected.
   - `page=abc` (non-numeric): `400`.
   - `project=TH&pageSize=2&page=2`: `totalItems` reflects only the `TH` project's issue count, and the
     returned page contains only `TH-*` issues -- filtering and pagination compose correctly.
   - `GET /api/v1/issues/export.csv` and `GET /api/v1/dashboard`: unaffected, confirming the unpaginated
     `listIssues(filter)` overload and its callers are untouched by this batch.
6. Browser regression: re-ran the existing `reorder_move_bulk_test.mjs` and `login_browser_test.mjs`
   Playwright scripts (the demo UI's heaviest consumers of `GET /api/v1/issues`) against the running
   server; all assertions passed unchanged, confirming the additive response-envelope change (new fields
   alongside the existing `items` array) does not break the UI's existing `result.items` consumption.

## 2026-08-02 — Security hardening pass: Phase 6 slice 7

Seventh Phase 6 slice, directly following the fixed request/batch-size constants batch. This is the
roadmap's "security hardening pass: dependency review, header review, session/CSRF review against
`handoff/KNOWN_CONSTRAINTS_AND_RISKS.md`" item. **Note:** `handoff/KNOWN_CONSTRAINTS_AND_RISKS.md` does
not exist anywhere in this repository -- only `docs/HANDOFF_NOTES.md` (a general onboarding doc, not a
risk register) exists. The review below was performed as a direct code-level audit instead of against
that (missing) file. With this batch done, Phase 6's only remaining item is numbered/offset pagination
(D126).

### What changed -- a real vulnerability found and fixed

While reviewing response headers, found that `GET /api/v1/attachments/{id}/download` always served
`Content-Disposition: inline`, and that an attachment's `contentType` is entirely caller-supplied and
unvalidated (D98 deliberately has no upload-time MIME allow-list, and the blocked-extension list only
blocks executable-ish extensions like `.exe`/`.bat`, not `.txt`/other safe-looking names). Concretely: any
project member could upload a file named e.g. `notes.txt` with its multipart `Content-Type` part spoofed
to `text/html` and a `<script>` payload as the body. Two exploitable paths followed from this:

1. **Direct download-URL navigation**: `Content-Disposition: inline` + `Content-Type: text/html` means a
   browser navigating straight to the download URL renders the attacker's HTML as a same-origin document,
   executing the embedded script with access to the app's origin (including the readable, non-HttpOnly
   `th_csrf` cookie -- enough to forge CSRF-protected write requests).
2. **The app's own "text" preview**: `attachmentPreviewKind` classifies anything with a `text/*`
   content-type (which includes a spoofed `text/html`) as `'text'`, and the issue drawer rendered that
   preview inside an `<iframe src="...">` with **no `sandbox` attribute** -- so even a user who never
   directly visits the download URL, but simply clicks "preview" on the malicious attachment inside the
   app's own UI, would trigger same-origin script execution.

This is a real stored-XSS / CSRF-bypass chain reachable by any project member against any other user
(including a global administrator) who previews or opens the malicious attachment -- a genuine privilege-
escalation path, not a theoretical one. Fixed with two independent, complementary layers:

- **`web/app.js`**: both the PDF and text `<iframe>` previews now carry `sandbox=""` (no flags at all --
  disables script execution, plugins, forms, and top-level navigation from inside the iframe
  unconditionally, regardless of what content-type/disposition the server ever serves). This is the
  load-bearing fix, since it closes the vulnerability regardless of any Content-Disposition subtlety.
- **`src/web/Api.cpp`**: new `contentTypeSafeToRenderInline(contentType)` — an explicit deny-list of
  document/script-capable MIME prefixes (`text/html`, `application/xhtml*`, `image/svg*`,
  `application/xml`, `text/xml`, `application/xslt`, `application/javascript`, `text/javascript`,
  `application/ecmascript`). The download route now serves `Content-Disposition: attachment` (forced
  download, never rendered as a document) for anything matching that deny-list, and `inline` (unchanged
  behavior) for everything else -- closing the direct-URL-navigation path independently of the iframe fix.
  `<img>`/`<audio>`/`<video>` tag rendering is unaffected either way, since those elements do not honor
  `Content-Disposition` (confirmed: a real PNG upload still serves `inline` and previews via `<img>`
  exactly as before).

### What changed -- standard security headers

- New `applySecurityHeaders(response)` in `Api.cpp`, called from every response-construction chokepoint
  (`jsonResponse`, and the two routes that build a `crow::response` directly -- the CSV export and the
  attachment download): `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`,
  `Referrer-Policy: same-origin`.
- New `applyStaticSecurityHeaders(response)` in `HttpServer.cpp`, applied to every static asset response
  (`/`, `/app.js`, `/styles.css`, `/favicon.svg`): the same three headers.
- New `applyHtmlSecurityHeaders(response)`, applied only to `/` (the HTML document itself, the only
  response type where a CSP is meaningful): adds
  `Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline';
  img-src 'self' data:; object-src 'none'; base-uri 'self'; frame-ancestors 'none'; form-action 'self'`.
  `script-src 'self'` is the load-bearing clause -- it blocks any injected/inline `<script>` from
  executing at all, a second layer of defense behind the app's own Markdown-HTML sanitization and the
  now-sandboxed attachment-preview iframes. `style-src` allows `'unsafe-inline'` deliberately: `web/app.js`
  renders many inline `style="..."` attributes for layout (not user-controlled content), and reworking
  that to CSS classes is out of scope for this pass -- inline styles cannot execute script, so this is a
  low-risk, explicit exception, not an oversight.

### Dependency review

Crow (the only fetched third-party dependency, via `FetchContent`) is pinned to release tag `v1.3.3`, not
a floating branch -- already good practice, no change needed. SQLite3/PostgreSQL/Argon2 are resolved via
`find_package` against system-installed libraries, not vendored/fetched, so there is no supply-chain
pinning concern to add there either. No dependency changes in this batch.

### Session/CSRF review

Re-confirmed (unchanged from prior batches, no regressions introduced by the PAT/rate-limiting/versioning
work earlier this session): the session cookie is `HttpOnly; Secure; SameSite=Strict`; the CSRF cookie is
`Secure; SameSite=Strict` (deliberately not `HttpOnly`, since client-side JS must read it to echo as the
`X-CSRF-Token` header); `csrfTokenValid` still correctly requires an exact cookie/header match and is
still correctly exempted only when no session cookie is present at all (PAT/Bearer-authenticated
requests). No changes needed here; already matches CLAUDE.md's security rules.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 8/8 green -- this batch is header/response-shape changes plus one
   deny-list function with no domain/database surface, so the existing suite (which never asserts on
   response headers) is unaffected and still exercises every write-path behavior unchanged.
3. No live-PostgreSQL check: no database or `IDatabase` changes in this batch.
4. End-to-end HTTP verification via `curl` against a locally running server (SQLite, demo-seeded):
   - Confirmed `X-Content-Type-Options`/`X-Frame-Options`/`Referrer-Policy` present on `/`, `/app.js`, and
     `/api/health` responses; confirmed `Content-Security-Policy` present only on `/`.
   - Uploaded a file named `evil.txt` with a spoofed `Content-Type: text/html` multipart part; confirmed
     the download response now serves `Content-Disposition: attachment` (previously `inline`).
   - Uploaded a real PNG with `Content-Type: image/png`; confirmed the download response still serves
     `Content-Disposition: inline` exactly as before (no regression for legitimate image previews).
5. Browser verification via Playwright/Chromium:
   - Re-ran the existing attachment-preview-kinds test (PDF/text/audio/video) against the sandboxed
     iframes: all four preview kinds still render their expected tag (`<iframe>` for pdf/text,
     `<audio>`/`<video>` for the others) exactly as before -- confirming `sandbox=""` does not break the
     legitimate preview functionality.
   - New targeted XSS-reproduction test: uploaded a `text/html`-spoofed `evil.txt` containing
     `<script>alert("XSS-MARKER: " + document.cookie)</script>`, clicked its preview in the issue drawer
     (the exact vulnerable path found above), and confirmed **no `dialog` event fired** (Playwright's
     `page.on('dialog')` would catch an `alert()` call) -- the payload did not execute. Before the
     `sandbox=""` fix this same script would have fired the dialog; this test is the regression guard
     against the fix being accidentally reverted.

## 2026-08-02 — Fixed request/batch-size constants (D125): Phase 6 slice 6

Sixth Phase 6 slice, directly following the CSV export batch. Still open: numbered pagination (D126,
which max page size from D125 is coupled to and remains blocked on) and the security hardening pass.

### What changed

- New `MaxJsonRequestBodyBytes` (1 MiB) constant in `Api.cpp`, enforced via a scripted substitution at
  all 20 `const auto body = crow::json::load(request.body);` call sites (a single exact-text pattern,
  confirmed identical via the same kind of pre-edit regex scan used for the CSRF/rate-limit chokepoints
  in earlier batches): a `request.body.size() > MaxJsonRequestBodyBytes` check immediately precedes the
  parse, returning `413 Payload Too Large` before any JSON parsing happens. Enforced against the already-
  buffered `request.body.size()`, not against the `Content-Length` header pre-buffer -- Crow's SimpleApp
  has no built-in hook for the latter, so this caps what the application processes rather than what the
  socket layer buffers. Documented as a known, honest limitation: a real internet-facing deployment
  should also enforce a body-size limit at a reverse proxy in front of this server. The multipart
  attachment upload route is untouched -- it already enforces its own stricter, purpose-built 25MB/file
  limit (D98) via `Domain::validateAttachmentUpload`, unrelated to this constant.
- New `MaxBulkItems` (200) constant, enforced in the single shared `requiredIssueKeys(body)` helper that
  all four `POST /api/v1/issues/bulk/*` routes already called -- one chokepoint, no route-by-route
  changes needed. Throws `std::invalid_argument` (mapped to the existing `400` handler in all four
  routes) when `issueKeys.size() > MaxBulkItems`.
- Max page size intentionally not implemented: it has no meaning until numbered/offset pagination (D126)
  exists, and faking a page-size cap on today's fully-unpaginated list endpoints would not match D125's
  intent. Documented in-source and here rather than silently skipped.
- No admin configuration for either enforced limit, matching D125's "no admin exceptions."
- No database/migration changes.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 8/8 green -- unchanged; this batch adds size checks ahead of existing
   logic and does not change behavior for any request within the limits, which is what every existing
   test exercises.
3. No live-PostgreSQL check: no database or `IDatabase` changes in this batch.
4. End-to-end HTTP verification via `curl` against a locally running server (SQLite, demo-seeded):
   - A ~1.05MB JSON issue-creation body returned `413` with body `{"error":"Request body too large"}`.
   - A bulk-label request with 201 `issueKeys` returned `400` with body
     `{"error":"issueKeys must not contain more than 200 items"}`; the same request with exactly 200
     `issueKeys` returned `200` (confirming the boundary is `> 200`, not `>= 200`).
   - A normal-sized issue-creation request (well under 1 MiB) still returned `201` as before, confirming
     no regression for ordinary traffic.
5. Browser regression: re-ran the existing `reorder_move_bulk_test.mjs` Playwright script (reorder,
   move-to-another-project, and bulk actions -- the write paths most directly touched by this batch,
   since bulk actions now flow through the new `MaxBulkItems` check) against the running server; all
   assertions passed unchanged, confirming ordinary-sized traffic through the newly-touched call sites is
   unaffected.

## 2026-08-02 — Read-only CSV export of issues (D48): Phase 6 slice 5

Fifth Phase 6 slice, directly following the `/api/v1` versioning batch. Still open: fixed request/body/
batch-size constants, numbered pagination, and the security hardening pass.

### What changed

- New `GET /api/v1/issues/export.csv` route in `Api.cpp`. Shares `Domain::IssueFilter`'s query-parameter
  parsing with the existing `GET /api/v1/issues` JSON list route via a new `issueFilterFromQuery(request)`
  helper (factored out of both routes) and the same `resolvePrincipal`-based authorization -- an export is
  always scoped to whatever the caller could already see via the list view. No CSRF/rate-limit check,
  matching every other read-only GET route.
- New `csvField(value)` helper (RFC 4180-style: quote-wraps a field containing a comma, quote, or
  newline, doubling internal quotes) and `issuesToCsv(issues)` (builds the full CSV body, `\r\n` line
  endings). Columns: key, project, summary, description, type, status, priority, reporter, assignee,
  storyPoints, dueDate, resolution, labels (semicolon-joined within the field, itself still subject to
  `csvField` escaping), createdAt, updatedAt.
- Route ordering: `/api/v1/issues/export.csv` sits at the same path depth as the parameterized
  `/api/v1/issues/{key}` route. Confirmed via `curl` that Crow's trie router resolves the static segment
  first (matching the existing precedent of `/api/v1/issues/deleted` and `/api/v1/issues/bulk/*`
  coexisting with `/api/v1/issues/{key}` without incident).
- `web/app.js`: factored the existing ad-hoc `fetchIssues()` query-building into a reusable
  `issueFilterParams()` function, then used it to build the `href` of a new "Export CSV" link
  (`<a class="secondary-button" download="issues.csv">`) placed in the Issues view's page-actions row,
  next to the recycle-bin toggle. Hidden in the recycle-bin view (nothing meaningful to export there).
  Re-rendered on every filter change, so the link's `href` always matches the currently visible/filtered
  issue set. New `a.secondary-button` CSS rule (`display: inline-block; text-decoration: none; cursor:
  pointer;`) since this class had only ever been applied to `<button>` elements before.
- No database/migration changes, no new domain/application code -- purely an `Api.cpp` route plus a thin
  `web/` affordance.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 8/8 green -- unchanged, since `csvField`/`issuesToCsv`/the new route are
   pure `Api.cpp`/HTTP-layer code with no domain/application/database surface to unit-test against,
   verified instead via `curl` and a real browser download, matching how this project has verified other
   Api.cpp-only route additions (e.g. the PAT Bearer-auth exemption).
3. No live-PostgreSQL check: no database or `IDatabase` changes in this batch.
4. End-to-end HTTP verification via `curl` against a locally running server (SQLite, demo-seeded):
   confirmed `Content-Type: text/csv; charset=utf-8` and `Content-Disposition: attachment;
   filename="issues.csv"` headers; confirmed the CSV body lists every seeded issue with correctly-quoted
   fields (a summary/description containing a literal comma round-tripped correctly, wrapped in quotes);
   confirmed `?project=WEB` scopes the export to only that project's two seeded issues; confirmed an
   unauthenticated request (anonymous read disabled, the default) returns `401`; confirmed
   `GET /api/v1/issues/TH-1` (the parameterized `{key}` route) and the plain `GET /api/v1/issues` list
   route both still resolve correctly alongside the new static `export.csv` route.
5. Browser verification via Playwright/Chromium: logged in, navigated to the Issues view, captured the
   real browser-initiated download (`page.waitForEvent('download')`, not just an HTTP assertion) from
   clicking the new "Export CSV" link, confirmed the suggested filename (`issues.csv`) and row count
   matched the seeded data; filtered by `project=WEB` in the UI, confirmed the link's `href` updated to
   include `project=WEB`, re-downloaded, and confirmed every row in the new file starts with `WEB-`.

## 2026-08-02 — Versioned `/api/v1` prefix (D127): Phase 6 slice 4

Fourth Phase 6 slice, directly following the rate-limiting batch. Still open: fixed request/body/batch-
size constants, numbered pagination, CSV export, and the security hardening pass.

### What changed

- Every `CROW_ROUTE(app, "/api/...")` registration in `Api.cpp` (70 total) moved to `/api/v1/...`, via a
  single scripted regex substitution, except `GET /api/health`, deliberately kept unversioned --
  following the common convention that infra/monitoring health checks live outside API versioning. Not
  specified by any decision text; a conservative choice documented here explicitly. A handful of
  in-source comments referencing exact routes were updated to match.
- `web/app.js`'s ~63 API call sites (there is no single base-URL constant -- every call site hardcodes its
  own `/api/...` path string, including inside template literals) were updated the same way, via a
  matching scripted regex substitution over the whole file, again sparing `/api/health`.
- No server/domain/database logic changed anywhere -- this batch is a pure URL rename with zero behavior
  change beyond the URL itself.
- Documentation: updated the *living* current-state references (`README.md`'s API section/route table,
  `docs/SCOPE.md`, `docs/SCHEMA.md`, `PLAN.md`) to the new `/api/v1/...` paths. Deliberately did **not**
  rewrite the dated, historical batch narratives in this file or in `CHANGELOG.md`/`NEXT.md`'s
  per-batch bullets -- those describe what was literally true at the time each entry was written (under
  whichever prefix was live then), matching this project's existing convention of never retroactively
  editing past dated log entries.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 8/8 green -- this batch touched no test-relevant logic (the existing
   integration tests call `TicketService`/`AuthService` directly, never over HTTP, so none of them
   reference a literal `/api/...` path).
3. No live-PostgreSQL check: this batch made no database or `IDatabase` changes, and HTTP routing is
   identical regardless of backend, so there is nothing backend-specific to verify against a live server.
4. End-to-end HTTP verification via `curl` against a locally running server (SQLite, demo-seeded):
   `GET /api/health` still returns `200` unversioned; the old unversioned `GET /api/projects` now returns
   `404`; `POST /api/v1/auth/login` and an authenticated `GET /api/v1/projects` both work exactly as the
   unversioned routes did before.
5. Full browser regression pass with the existing Playwright/Chromium suite (unmodified from prior
   batches, run against the renamed routes to confirm `web/app.js` has no remaining hardcoded old-prefix
   paths): login/logout; full project lifecycle (create, duplicate-key rejection, archive, delete,
   recycle-bin restore/permanent-delete, non-admin 403 on create); issue watch/vote toggling, cloning,
   issue links (add/click-navigate/delete), full-replacement edit with save/cancel; manual reorder
   (move-up/move-down with correct renumbering), move-to-another-project, and bulk actions (multi-select,
   bulk label with a toast reporting succeeded/failed counts). All green, confirming the UI has zero
   remaining references to the unversioned prefix.

## 2026-08-02 — Fixed rate limits (D124/D125): Phase 6 slice 3

Third Phase 6 slice, directly following the active-session-list batch. Still open: the versioned
`/api/v1` prefix, fixed request/body/batch-size constants, numbered pagination, CSV export, and the
security hardening pass.

### What changed

- New `TicketHub::Web::RateLimiter` (`src/web/RateLimiter.h/.cpp`): a thread-safe, in-memory, fixed-window
  counter keyed by an arbitrary string, with lazy periodic sweeping of expired buckets (every 512 calls)
  so the map does not grow unbounded over a long-running process. Two hardcoded instances, matching D124's
  "simple fixed rate limit per IP/user ... no admin config, no per-endpoint/service-account exceptions"
  exactly:
  - `loginRateLimiter()`: 20 attempts per IP (`remote_ip_address`) per 15 minutes, checked at the top of
    `POST /api/auth/login` before the request body is even parsed.
  - `writeRateLimiter()`: 120 requests per minute, keyed by `user:<id>` when `resolvePrincipal` resolves a
    caller, else `ip:<remote_ip_address>`.
- The write limiter deliberately reuses the same near-universal chokepoint as CSRF checking: all 43
  existing `if (!csrfTokenValid(request)) { ... }` blocks across `Api.cpp` are byte-identical (verified via
  a Python regex scan before editing), so a single scripted text substitution inserted a
  `writeRateLimitOk(request, principal)` check immediately after each one. The sole exception,
  `/api/sessions/sign-out-others`, resolves a `Domain::Session` (`current`) rather than a
  `Domain::Principal`, so it uses a dedicated `writeRateLimitOk(request, const std::string& userId)`
  overload instead.
- Deliberately does **not** touch the existing per-account login lockout
  (`recordFailedLogin`/`resetFailedLogin`/`isLoginLocked`, fixed 10-attempts/15-minutes, in both database
  adapters) -- the new IP-based limiter is an additional, complementary layer defending against
  distributed/enumeration attacks spread across many accounts from one source, not a replacement for the
  per-account lockout.
- `RateLimiter` is compiled into the `ticket-hub` server executable's own source list (not
  `ticket-hub-core`), since rate limiting is HTTP-layer-only and the CLI target has no use for it. It has
  no Crow dependency, so it is also compiled directly (with `src/web/RateLimiter.cpp`, no `ticket-hub-core`
  link) into a new standalone `ticket-hub-ratelimiter-tests` binary that builds unconditionally under
  `TICKETHUB_BUILD_TESTS`, independent of `TICKETHUB_BUILD_SERVER`.
- Both 429 responses carry a `Retry-After` header (900 seconds for the login limiter, 60 for the write
  limiter) via a small `rateLimitedResponse(message, retryAfterSeconds)` helper -- the original
  (pre-simplification) description of D124 paired 429 with Retry-After, and the V1 simplification text
  only drops the admin-configurable multi-level limits, not that response contract.
- No new migration; no `IDatabase` changes; no web UI changes (a 429 response surfaces through the
  existing generic API-error handling like any other error status).

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 8/8 green (the new eighth binary is `ticket-hub-ratelimiter-tests`: a
   fixed-window limit trips after N requests and rejects further ones within the same window; independent
   keys get independent buckets; the window rolls over and requests are allowed again after it expires).
3. Re-ran the SQLite-only and PostgreSQL-only build configurations (`-DTICKETHUB_BUILD_SERVER=OFF` in
   both): both compile cleanly, and `ticket-hub-ratelimiter-tests` builds and passes in both (8/8 and 4/4
   respectively, matching each configuration's existing test-binary count plus one) -- confirming
   `RateLimiter` really has no Crow/database dependency.
4. No live-PostgreSQL check: this batch made no database or `IDatabase` changes, so there is nothing
   backend-specific to verify against a live server.
5. End-to-end HTTP verification via `curl` against a locally running server (SQLite, demo-seeded):
   - Login limiter: 20 consecutive `POST /api/auth/login` requests with a wrong password all returned
     `401`; the 21st and 22nd returned `429` with body `{"error":"Too many login attempts. Try again
     later."}` and header `Retry-After: 900`.
   - Write limiter: after restarting the server (a fresh in-memory limiter) and logging in with the demo
     account, 130 consecutive `POST /api/projects` requests (valid session cookie + `X-CSRF-Token`) were
     issued back-to-back: the first 120 all returned non-429 status codes and the last 10 all returned
     `429` with body `{"error":"Too many requests. Try again later."}` and header `Retry-After: 60`.
   - Confirmed reads are unaffected: a `GET /api/projects` issued immediately after tripping the write
     limit still returned `200`, confirming the limiter only applies to write methods.
6. No browser/Playwright verification needed -- this batch touched no `web/` code, and there is no UI
   surface for a 429 beyond the existing generic fetch-error handling already exercised by prior batches.

## 2026-08-01 — Active-session list and "sign out everywhere" (D54): Phase 6 slice 2

Second Phase 6 slice, directly following the PAT batch. Still open: the versioned `/api/v1` prefix, rate
limits and the full lockout policy, CSV export, and the security hardening pass.

### What changed

- `IDatabase::listSessionsForUser(userId)` (every non-expired session, newest first) and
  `deleteOtherSessionsForUser(userId, keepSessionId)` (deletes every session for that user except
  `keepSessionId`, returns the count removed) in both adapters -- no new migration, `sessions` already had
  everything needed.
- `AuthService::currentSession(sessionToken)` resolves the session row itself (not just the `Principal`
  `validateSession` returns), so a caller can identify which entry in a session list is "this one."
  `listActiveSessions`/`signOutOtherSessions` are thin wrappers over the new `IDatabase` methods.
- **Conservative default, no decision text specifies this:** "sign out everywhere" keeps the caller's own
  current session active and only removes the others -- the request making the call should never lock its
  own caller out, matching the common GitHub/Google pattern. Documented here explicitly since the roadmap
  only says "active-session list and sign out everywhere endpoint" without this detail.
- New `GET /api/sessions` and `POST /api/sessions/sign-out-others` routes. Deliberately session-cookie-only
  (checked directly via the cookie, not through `resolvePrincipal`, which would also accept a PAT Bearer
  token): "your active web sessions" has no meaning for a PAT-authenticated caller, since a PAT is not a
  session at all.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 7/7 green. New `identity_integration_tests` coverage: three concurrent
   sessions for one user all appear in `listActiveSessions`; `currentSession` resolves the right row (and
   is `nullopt` for an unknown token); `signOutOtherSessions` removes exactly the other two, the caller's
   own session survives (`validateSession` still succeeds on it), and the two removed sessions no longer
   validate.
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile cleanly.
4. Live PostgreSQL verification: a standalone smoke-test program (not committed) created three sessions
   directly, confirmed `listSessionsForUser` returns all three, confirmed `deleteOtherSessionsForUser`
   removes exactly two and returns `2`, and confirmed the kept session still resolves via
   `findSessionByTokenHash` while the removed two do not. All passed; database dropped afterward.
5. End-to-end HTTP verification via `curl` against a locally running server (SQLite, demo-seeded): logged
   in twice as `demo` (simulating two browser tabs) with two independent cookie jars; `GET /api/sessions`
   from tab A correctly listed both sessions with `isCurrent` set on the right one;
   `POST /api/sessions/sign-out-others` from tab A returned `{"signedOutCount":1}`; confirmed tab A's own
   session still authenticates (`200` on `/api/auth/me`) while tab B's now returns `401`; confirmed a
   follow-up `GET /api/sessions` from tab A shows only the one remaining (current) session.
6. A quick regression check (not the full suite, since nothing in `web/` changed and this batch is purely
   backend/API): re-ran the comment-editing and board-WIP-limits browser tests, both still passing
   unchanged, confirming ordinary cookie-based login and CSRF-protected writes were not affected by the
   `resolvePrincipal`/`csrfTokenValid` changes made in the prior PAT batch.

All checks passed. No committed test scripts (scratchpad only).

### What is still not built

Phase 6 (Milestone 3), continued: the versioned `/api/v1` prefix, fixed rate limits on login/write
endpoints and the full lockout policy (today's is still the minimal Phase 1 version), fixed
request/body/batch-size constants, numbered/offset pagination, read-only CSV export of issues, and the
security hardening pass. No web UI yet for viewing/signing-out sessions or managing tokens.

## 2026-08-01 — Personal access tokens (D39/D40): Phase 6 slice 1

First Phase 6 (Milestone 3) slice: PAT-only API authentication. The rest of Phase 6 (a versioned
`/api/v1` surface, fixed rate limits and the full lockout policy, active-session list, CSV export, and
the security hardening pass) is untouched.

### What changed

- Migration `015_personal_access_tokens.sql` (both backends) adds `personal_access_tokens`, mirroring
  `sessions` (`id`, `user_id`, `token_hash` unique, `created_at`, `expires_at`), plus `name` (a
  user-chosen label), `last_used_at` (nullable), and `revoked_at` (nullable -- a token can be revoked
  before it naturally expires). D40's "basic token security": hashed storage, expiration, revocation,
  last-used tracking; no scopes (a token carries exactly its owner's permissions), no rotation, no
  admin-configurable max lifetime.
- `Domain::PersonalAccessToken` (metadata only) and `Domain::CreatedPersonalAccessToken` (adds the raw
  token, returned only once, at creation, never stored or logged). `IDatabase::createPersonalAccessToken`/
  `findPersonalAccessTokenByHash`/`listPersonalAccessTokens`/`revokePersonalAccessToken`/
  `touchPersonalAccessTokenLastUsed` in both adapters -- `findPersonalAccessTokenByHash` mirrors
  `findSessionByTokenHash` exactly, filtering out expired/revoked tokens at the SQL layer so a present
  result always means "currently valid."
- `AuthService::createPersonalAccessToken` (requires a name and a positive `expiresInDays` -- D40 requires
  an expiration, there's no "never expires" option), `listPersonalAccessTokens`, `revokePersonalAccessToken`
  (ownership-scoped: only the owner can revoke their own token), `validatePersonalAccessToken` (mirrors
  `validateSession`, additionally touches `last_used_at` on success).
- `Api.cpp`'s `resolvePrincipal` now also checks an `Authorization: Bearer <token>` header when no
  session cookie is present -- cookie and Bearer auth are mutually exclusive per request (D54: sessions
  for web, PATs for API). `csrfTokenValid` now returns `true` unconditionally when no session cookie is
  present: CSRF only defends against a browser silently attaching a cookie to a forged request, and a
  Bearer token is never auto-attached, so a PAT-authenticated write needs no CSRF header. This required no
  changes to any of the ~50 existing route handlers that already call `csrfTokenValid`. New self-service
  `GET`/`POST /api/tokens` and `DELETE /api/tokens/{id}` routes (session-cookie-authenticated, since
  managing your own tokens is a web-UI action even though the tokens themselves authenticate API calls).

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 7/7 green. New `identity_integration_tests` coverage: create/validate/
   list/revoke, last-used-at updates on validation, ownership enforcement (a non-owner's revoke attempt
   fails and the token survives), a revoked token no longer validates, and both `expiresInDays <= 0` and
   an empty name are rejected.
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile cleanly.
4. Live PostgreSQL verification: a standalone smoke-test program (not committed) exercised
   `PostgresDatabase`'s PAT methods directly -- create/find/touch-last-used/list/revoke, and confirmed
   both a revoked token and a separately created already-expired token both fail to resolve via
   `findPersonalAccessTokenByHash` while still appearing in `listPersonalAccessTokens` (so a user can see
   and manage tokens that have already lapsed). All passed; database dropped afterward.
5. End-to-end HTTP verification via `curl` against a locally running server (SQLite, demo-seeded, no
   Playwright for this slice since it has no web UI yet): created a token via cookie-authenticated
   `POST /api/tokens`; used the raw token as a Bearer header with **no cookies at all** to read
   `GET /api/auth/me` (resolved correctly) and to **write** via `POST /api/projects` with **no CSRF
   header** (succeeded, confirming the CSRF exemption works end-to-end through the real HTTP layer, not
   just in isolation); confirmed `GET /api/tokens` (cookie-authenticated) shows the updated `lastUsedAt`;
   revoked the token via `DELETE /api/tokens/{id}`; confirmed the same Bearer token now gets a 401.

All checks passed. No committed test scripts (scratchpad only).

### What is still not built

Phase 6 (Milestone 3) has just started. Still open: the versioned `/api/v1` surface itself (routes exist
today only under the unversioned `/api/*` prefix used by both the web UI and, as of this batch, PATs);
fixed rate limits on login/write endpoints and the full configurable-replacing lockout policy (today's
lockout is still the minimal Phase 1 version); the active-session list and "sign out everywhere" endpoint;
fixed request/body/batch-size constants and numbered/offset pagination; read-only CSV export of issues;
and the security hardening pass (dependency review, header review, session/CSRF review). There is also no
web UI yet for a user to create/view/revoke their own tokens -- `/api/tokens` is fully functional but only
reachable via `curl`/scripts today.

## 2026-08-01 — Attachments (D15/D98-D105): Phase 5 complete

Fourth and final Phase 5 slice. With this batch, every item in `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 5
list is implemented, closing out Milestone 2.

### What changed

- Migration `014_attachments.sql` (both backends) adds only an `idx_attachments_issue` index --
  `attachments.sha256`/`deleted_at`/`deleted_by_user_id` already existed from
  `003_product_foundation.sql`, pre-provisioned well ahead of this phase. An early draft of this
  migration re-added those three columns and failed on a live run with "duplicate column name: sha256";
  caught immediately by `ctest`, not shipped.
- `Domain::Attachment` (`id`, `issueId`, `issueKey` -- resolved via a join purely for the recycle bin's
  display convenience, `uploader`, `fileName`, `contentType`, `byteSize`, `sha256`, `createdAt`,
  `deletedAt` -- set only when returned from the recycle bin). `IDatabase::createAttachment` is the one
  create* method that takes a caller-supplied `id` rather than generating one internally: the local
  filesystem storage key (D15, hardwired, no storage-backend abstraction) must be known -- and the file
  already written -- before the row is inserted, so a database row never describes a file that doesn't
  exist on disk. `listAttachments`/`findAttachmentById`/`softDeleteAttachment`/`restoreAttachment`/
  `listDeletedAttachments`/`permanentlyDeleteAttachment` in both adapters, mirroring the
  issue/project/comment tombstone pattern. `listAttachmentStorageKeysForIssue`/`...ForProject` return
  every attachment's storage key regardless of soft-delete state -- used to delete files on disk before a
  permanent issue/project delete cascades through the database (there is no periodic orphan-file audit at
  all, D105, so this is the only cleanup path for files whose row is about to disappear via
  `ON DELETE CASCADE`).
- New `src/infrastructure/storage/LocalAttachmentStorage`: a plain, non-virtual class (D15 -- no abstract
  storage port/interface, so no S3-shaped extension point either), with `save`/`read`/`remove`/`sizeOf`
  against a flat directory, keyed by the attachment's own UUID. Defends against path traversal even
  though the key is always self-generated, never user input. Root directory is
  `TICKETHUB_ATTACHMENTS_DIR` (default `./data/attachments`, added to `.gitignore`).
- `Domain::validateAttachmentUpload` (D98): fixed 25MB/file, 20 attachments/issue, and a denylist of
  common executable/script extensions -- no admin configuration, no MIME allow-list, no quotas, no
  antivirus/DLP (all explicitly out of scope per D98's own "no admin configuration" answer).
- `TicketService`: `uploadAttachment` (project-Member-or-above, matching every other issue write) computes
  the SHA-256 at upload time (D105, never re-verified) and calls the storage class before the database
  insert. `downloadAttachment`/`listAttachments` mirror comments' read-access rule (any authenticated
  user, or anonymous if the installation toggle is on) -- no project-role check. `deleteAttachment` is
  uploader-or-project-Admin-or-above -- no decision text addresses attachment deletion directly, so this
  mirrors D83's comment edit/delete rule as the closest precedent. `listDeletedAttachments` implements
  D102's fixed 90-day on-demand retention itself (unlike `listDeletedIssues`/`listDeletedProjects`, which
  purge with a single `DELETE` entirely at the SQL layer): purging an attachment also means deleting its
  file on disk, which the SQL-only `IDatabase` layer cannot do, so the age check and the file removal both
  happen here, one layer up. `permanentlyDeleteIssue`/`permanentlyDeleteProject` were both extended to
  collect every affected attachment's storage key and delete its file *before* the database cascade runs.
- New routes: `GET`/`POST /api/issues/{key}/attachments` (list, and a `multipart/form-data` upload with a
  single `file` part), `DELETE /api/issues/{key}/attachments/{id}`,
  `GET /api/attachments/{id}/download` (not nested under `/issues/{key}` -- a download link, or an inline
  `<img>`/`<audio>`/`<video>`/`<iframe>` preview `src`, only ever needs the attachment id, since it can be
  referenced via `attachment://<id>` from any comment on the issue, not just its description),
  `GET /api/attachments/deleted`, `POST /api/attachments/{id}/restore`,
  `DELETE /api/attachments/{id}/permanent` (all three admin-only, matching the issue/project recycle
  bins). The whole upload body is read into memory before `Domain::validateAttachmentUpload` runs (no
  streaming/early-abort on an oversized request) -- an accepted V1 simplification, not something any
  decision calls for.
- `web/`: the issue drawer gained an "Attachments" section -- a sortable list (D101: name/size/date/
  uploader/type, client-side, matching the "ad-hoc" philosophy used for issue filters), an "Attach files"
  button plus a real drag-and-drop dropzone, and per-row delete (shown when the uploader matches the
  current user or they're a global admin, the same "client-side approximation, server enforces the real
  rule" pattern already used for comment edit/delete buttons). Clicking a file name toggles an inline
  native-element preview for the four D99 kinds (`<img>` for images, `<iframe>` for PDF and text,
  `<audio>`/`<video>` for the rest) or opens a new tab for anything else. The Markdown toolbar
  (`attachMarkdownToolbar`) gained an optional third capability, wired up everywhere an issue key is
  already known (both comment textareas, the issue-edit description) but deliberately left off the
  create-issue form's description field, since no issue exists yet to attach a file to: a 📎 toolbar
  button, real native drag-and-drop onto the textarea, and clipboard paste, all uploading and inserting
  `![name](attachment://id)` (images) or `[name](attachment://id)` (everything else) at the cursor.
  `renderMarkdownInline` gained real `![alt](url)` image-syntax support (it previously had none -- an
  unescaped literal `!` followed by a plain link) and resolves `attachment://<id>` in both image and link
  syntax to `/api/attachments/<id>/download`, validating the id shape first and leaving anything
  malformed as inert literal text, the same "safe by construction" posture as the existing
  `javascript:`-scheme guard. A new admin-only "Attachment recycle bin" nav item mirrors the audit log's
  visibility pattern (hidden by default, shown only for global admins, re-hidden on logout) and lists
  every deleted attachment across every issue (using the new `issueKey` field, since a raw internal UUID
  would be meaningless here) with restore/permanent-delete actions.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 7/7 green. New `sqlite_integration_tests` coverage: full CRUD (create with
   a caller-supplied id, list, find, soft-delete/restore/permanent-delete, the tombstone semantics),
   `listAttachmentStorageKeysForIssue`/`...ForProject` returning keys regardless of soft-delete state, and
   rejection of an attachment on an unknown issue. New `authorization_integration_tests` coverage: upload
   permission (project-Member-or-above), delete permission (uploader-or-project-Admin-or-above, with both
   the "different member, not admin" rejection and the "global admin can delete anyone's" success case
   explicitly asserted), the fixed 25MB and blocked-extension limits, and the full recycle-bin
   authorization split (list/restore/permanent-delete all global-administrator-only). A first draft of two
   of these authorization assertions used `locator...begin()`/`.end()`-style double-evaluation (calling
   the same async listing method twice inside one `std::any_of`/`std::none_of` call, comparing iterators
   from two different temporary vectors) -- caught immediately by a nonsensical failure and fixed by
   binding the result to a local variable first.
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile cleanly.
4. Live PostgreSQL verification: a standalone smoke-test program (not committed) exercised
   `PostgresDatabase`'s attachment methods directly against a throwaway `tickethub_attach_test_*`
   database -- create/list/find/soft-delete/restore/permanent-delete, and both storage-key listing
   methods. The first run failed with "inconsistent types deduced for parameter $1" because the INSERT
   reused the same `$1` placeholder for both the `id` and `storage_key` columns (SQLite tolerates this;
   PostgreSQL's prepared-statement type inference does not when the two columns have different declared
   types) -- fixed by passing the id as two separate parameters. All cases passed after the fix; database
   dropped afterward.
5. Standalone Playwright/Chromium scripts, against a locally running server, SQLite, demo-seeded,
   `TICKETHUB_ATTACHMENTS_DIR` pointed at a scratch directory:
   - Uploaded a text file and an image via the "Attach files" button; confirmed both appear in the list
     with correct name/size/uploader/date; toggled the image's inline preview open and closed and
     confirmed a real `<img>` element appears/disappears; confirmed a `.exe` upload is rejected (a 400 from
     the server, a toast shown, never added to the list); confirmed the sort control actually reorders the
     visible rows; deleted an attachment and confirmed it's gone.
   - Confirmed all four D99 preview kinds render the correct native element: PDF and plain text both as
     `<iframe>`, audio as `<audio>`, video as `<video>` (each with a distinct fake file uploaded and its
     preview toggled open).
   - Confirmed real browser drag-and-drop (a synthetic `DataTransfer` dispatched as an actual `drop`
     event, not just `setInputFiles`) works both on the attachment dropzone and directly onto a comment's
     Markdown textarea, and that a real `paste` event with `clipboardData` also uploads and inserts a
     reference. A first attempt at the paste case used Playwright's `dispatchEvent(selector, 'paste',
     {clipboardData})` helper, which silently produced an event with `clipboardData` still `undefined`
     inside the page (apparently Playwright only special-cases `dataTransfer` for drag events, not
     `clipboardData` for clipboard events) -- fixed by constructing and dispatching a real
     `new ClipboardEvent('paste', {clipboardData})` from inside `page.evaluate` instead, entirely within
     the browser context.
   - Attached a file through the comment editor's 📎 toolbar button, posted the comment, and confirmed the
     rendered comment shows a real download link (with a 📎 marker) pointing at
     `/api/attachments/<id>/download` -- i.e. that `attachment://<id>` references actually resolve when
     rendered, not just when inserted.
   - Confirmed the attachment recycle bin: hidden from a non-admin, visible and populated for a global
     admin (showing the correct issue key via the new join), restore makes the attachment reappear in its
     issue, and permanent delete empties the bin.
   - A full regression re-run of the markdown/mentions/reactions/worklog/audit-log/comment-editing/
     filter-widening/dashboard-personalization/board-WIP-limits browser tests against the same build
     confirmed no regression. One dashboard-personalization assertion appeared to fail on a run late in a
     long back-to-back sequence of a dozen browser scripts against the same long-lived dev server; a
     targeted re-investigation with request/cookie logging showed the session switch and the underlying
     data were both correct the whole time -- the script's `waitForSelector` was satisfied by a *stale*
     element left over from the previous user's render (which persists until the next render actually
     starts), and the next render was simply delayed under accumulated load from a dozen prior scripts'
     SQLite traffic sharing one mutex-guarded connection. A longer explicit wait confirmed the dashboard
     was correct throughout; this was test-harness latency, not an application defect, and is recorded
     here precisely rather than glossed over.

All checks passed. No committed test scripts, fixtures, or screenshots (scratchpad only).

### What is still not built

Phase 5 (Attachments and Kanban board) is now fully implemented -- every item in
`docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 5 list is done, closing out Milestone 2. Drag-and-drop *board*
reordering (distinct from the drag-and-drop *attachment* upload built in this batch) remains optional UX
polish, not required by D32 or D33. The next roadmap phase is Milestone 3 (REST API v1/export,
backup/restore/upgrade).

## 2026-08-01 — Kanban board WIP limits (D32/D33): Phase 5 slice 3

Third Phase 5 slice. Only the full attachments vertical (D15/D98-D105) remains. Drag-and-drop board
reordering was deliberately left out of this batch: neither D32 nor D33 mentions it, and the roadmap's own
Phase 5 exit gate ("board usable end-to-end") is already satisfied by click-to-drawer status changes,
which existed before this batch. It stays listed as optional UX polish in `NEXT.md`, same as before.

### What changed

- Migration `013_board_columns.sql` (both backends) adds `board_columns` -- a **single flat,
  installation-wide** table (`id`, `status_id` UNIQUE FK to `issue_statuses`, `wip_limit` nullable,
  `sort_order`), one row per fixed workflow status. This follows `docs/REDUCED_SCOPE_DATA_MODEL.md`'s
  target schema literally: no `board_id`/`project_id` column at all, matching D32's "one board column
  equals one workflow status" and D33's "cheap: one numeric field per column" reasoning. A WIP limit set
  on a column therefore applies to that status's column on *every* project's board, not per-project --
  there is no per-project board identity in the reduced-scope model. `002_seed_demo.sql` seeds the five
  rows (unlimited except "In Progress", given a demo-friendly limit of 3).
- `Domain::BoardColumn`; `IDatabase::listBoardColumns()` (ordered by `sort_order`) and
  `setBoardColumnWipLimit(statusKey, optional<int>)` (returns `false` for an unknown status key) in both
  adapters.
- `TicketService::listBoardColumns` (same read-access rule as projects/issues) and
  `setBoardColumnWipLimit` (global-administrator-only, like the anonymous-read toggle -- there is no
  per-project board-admin concept to delegate to instead); an unknown status key throws
  `std::invalid_argument`.
- New `GET /api/board-columns` and `PUT /api/board-columns/{statusKey}` routes.
- `web/`: the Board view fetches board columns alongside issues and shows each column's live count as
  `N / limit` when a limit is set (plain `N` when unlimited), applying a `.over-limit` highlight class
  (soft, display-time-only -- moving or creating issues into an over-limit column is never blocked) when
  the count exceeds the limit. Global admins additionally see a small inline number input per column to
  set/clear its WIP limit (empty input clears it back to unlimited); non-admins see the count only.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 7/7 green. New `sqlite_integration_tests` coverage: five seeded columns in
   `sort_order`, the seeded "In Progress" limit of 3, an unlimited column reporting no limit, setting a
   limit and reading it back, clearing a limit back to unlimited, and setting a limit on an unknown status
   key returning `false`. New `authorization_integration_tests` coverage: any authenticated user can read
   board columns; setting a limit is global-administrator-only; a global admin's change is visible to any
   reader; an unknown status key throws.
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile cleanly.
4. Live PostgreSQL verification: a standalone smoke-test program (not committed) exercised
   `PostgresDatabase::listBoardColumns`/`setBoardColumnWipLimit` directly against a throwaway
   `tickethub_board_test_*` database -- the same five-column/seeded-limit/set/clear/unknown-key cases as
   the SQLite integration test. All passed; database dropped afterward.
5. Standalone Playwright/Chromium script, against a locally running server, SQLite, demo-seeded: logged in
   as sam (non-admin) and confirmed the board shows column counts (`"1 / 3"` for TH's seeded In Progress
   column) with no WIP-editor controls visible; logged in as demo (global admin) and confirmed the editor
   controls appear; created three more in-progress TH issues via the API to push the count to 4/3 and
   confirmed the over-limit highlight class appears; raised the limit to 10 through the real inline editor
   control and confirmed the highlight clears; cleared the limit back to unlimited; switched to the WEB
   project and confirmed its In Progress column reflects the same (cleared) limit, directly demonstrating
   the setting is genuinely installation-wide and not accidentally scoped to whichever project was open
   when it was changed.
6. Re-ran the markdown, mentions/notifications, comment-reaction, worklog, audit-log, comment
   edit/delete, filter-widening, and dashboard-personalization browser tests against the same build to
   confirm no regression -- all still pass unchanged.

All checks passed. No committed test scripts or screenshots (scratchpad only).

### What is still not built

Only the full attachments vertical (D15/D98-D105) remains in Phase 5: upload API, hardwired local
filesystem storage, upload-time SHA-256 verification, four native-element previews, sortable
list/recycle bin/90-day retention, and Markdown-editor upload/drag-drop/paste integration referencing
`attachment://UUID`. Drag-and-drop board reordering remains optional UX polish, not required by any
decision.

## 2026-08-01 — Personal dashboard widgets (D24): Phase 5 slice 2

Second Phase 5 slice. Kanban WIP limits/drag-and-drop (D33) and the attachments vertical
(D15/D98-D105) remain untouched.

### What changed

- `Domain::DashboardStats` gains `assignedToMe`, `watchedIssues`, and `upcomingDeadlines` (all
  `std::vector<Issue>`), alongside the pre-existing installation-wide counts and `recentIssues`. Matches
  D24's fixed personal-dashboard widget set (assigned issues, watched issues, recent activity, deadlines,
  simple stats) minus the active-sprint widget, which D24 itself drops since Scrum was removed for V1.
- New `IDatabase::listWatchedIssues(userId, limit)` in both adapters -- the reverse direction of the
  existing `listWatchers` (which lists watchers of one issue; this lists issues watched by one user),
  newest-updated first.
- `TicketService::dashboard` now personalizes for an authenticated actor: `assignedToMe` reuses the
  existing `listIssues` assignee filter (no new query) and excludes Done-category issues; `upcomingDeadlines`
  is derived from that same result set app-side (open issues with a due date, soonest first) rather than a
  second database round trip; `watchedIssues` calls the new `listWatchedIssues`. All three stay empty for
  an anonymous viewer (no personal identity to personalize for) -- a conservative default, since D24's
  decision text describes this as a *personal* dashboard.
- `GET /api/dashboard` response gains `assignedToMe`/`watchedIssues`/`upcomingDeadlines` arrays.
- `web/`: the Dashboard view gains three new panels (reusing the existing `tablePanel` helper for
  "Assigned to me" and "Issues I'm watching"; a new small `deadlinesPanel` helper, structurally identical
  but with a Due date column, for "Upcoming deadlines") shown only when a principal is present.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 7/7 green. New `sqlite_integration_tests` coverage for `listWatchedIssues`
   (returns exactly the issues a given user is watching, empty for a user watching nothing). New
   `authorization_integration_tests` coverage for `TicketService::dashboard`: an anonymous viewer (with
   anonymous read temporarily enabled, since dashboard access itself requires read access) gets empty
   personal widgets; alex's `assignedToMe` excludes the Done-category TH-1 while including the open TH-3
   and WEB-1; `watchedIssues` reflects a newly watched issue.
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile cleanly.
4. Live PostgreSQL verification: a standalone smoke-test program (not committed) exercised
   `PostgresDatabase::listWatchedIssues` directly -- multiple users watching different/overlapping issues,
   the `limit` parameter capping the result, and cleanup unwatching returning to empty. All passed, against
   a throwaway `tickethub_dash_test_*` database, dropped afterward.
5. Standalone Playwright/Chromium script, against a locally running server, SQLite, demo-seeded: logged in
   as alex and confirmed the dashboard shows exactly three new panels in the expected order and that
   "Assigned to me" contains WEB-1 and TH-3 but not the Done-category TH-1; watched TH-2 through the real
   issue-drawer Watch button and confirmed the "Issues I'm watching" panel picks it up after returning to
   the dashboard; confirmed the deadlines panel shows its empty state when no seed issue has a due date,
   then separately set a real due date on TH-3 via the edit API and confirmed it appears in the deadlines
   panel with the correct formatted date; logged in as sam and confirmed sam's own "Assigned to me" shows
   sam's issues (TH-2, WEB-2) and not alex's TH-3, i.e. the personalization is actually per-actor and not a
   shared global list. A first draft of this browser script produced confusing results from state left over
   by an earlier failed run (a stale watch on TH-2 from a script that crashed on a drawer-backdrop click
   interception); re-running the script against a freshly reseeded server produced clean, correct results,
   confirming the earlier confusion was test-script state pollution, not an application bug.
6. Re-ran the markdown, mentions/notifications, comment-reaction, worklog, audit-log, comment
   edit/delete, and filter-widening browser tests against the same build to confirm no regression -- all
   still pass unchanged.

All checks passed. No committed test scripts or screenshots (scratchpad only).

### What is still not built

Phase 5's remaining items: Kanban board WIP limits and drag-and-drop (D33 plus the roadmap's own "usable
end-to-end" exit-gate wording), and the full attachments vertical (D15/D98-D105). Both untouched by this
batch.

## 2026-08-01 — Ad-hoc issue filter/search widening (D10/D43): Phase 5 slice 1

First Phase 5 (Attachments and Kanban board) slice. `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 5 list also
covers dashboard personalization (D24), Kanban WIP limits/drag-and-drop (D33), and the full attachments
vertical (D15/D98-D105); none of those are touched by this batch.

### What changed

- `Domain::IssueFilter` gains `issueTypeKey`, `priorityKey`, `assigneeEmail`, `label`, and `dueBefore`
  (all optional, alongside the pre-existing `projectKey`/`statusKey`/`search`) -- still the ad-hoc,
  in-UI-only filter model from D10: no saved/shared filters, no JQL, not usable as a webhook or board
  source.
- `SqliteDatabase::listIssues`/`PostgresDatabase::listIssues` both widened to the same 8-parameter
  `WHERE` clause: type/priority/assignee are plain equality joins against already-present aliases;
  `dueBefore` is an inclusive `<=` comparison; `label` is a fresh `EXISTS` subquery against
  `issue_labels`/`labels` (SQLite) or `issue_labels`/`labels` (PostgreSQL) rather than a condition on the
  already-aggregated label-list column, so a label filter narrows *which issues* match without dropping
  any of a matching issue's *other* labels from its displayed label list; `search` now also matches
  `i.description`, not just summary/issue key, per D43's "simple `LIKE`/`ILIKE` substring match, no
  full-text index" scope.
- `GET /api/issues` accepts new `type`, `priority`, `assignee`, `label`, and `dueBefore` query parameters.
  `TicketService::listIssues` needed no change -- it already passed the filter through unchanged.
- `web/`: the Issues view's filter bar gains type/priority/assignee dropdowns (fixed catalogs, matching
  the create-issue form's own hardcoded option lists), a label text input, and a due-date picker,
  alongside the pre-existing project/status/search controls. "Clear" and the Board/global-search
  transitions all reset the new fields too, so a lingering ad-hoc filter from the Issues view can't
  silently leak into the Board view or a fresh global search.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 7/7 green, including new `sqlite_integration_tests` assertions covering
   each new filter field individually, a combined multi-field filter, the inclusive `dueBefore` boundary,
   the widened description search, and an explicit check that filtering by label does not mutate or
   truncate the filtered issue's own label list.
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile cleanly.
4. Live PostgreSQL verification: a standalone smoke-test program (not committed) exercised the widened
   `PostgresDatabase::listIssues` directly -- created and edited an issue, then asserted the same
   type/priority/assignee/label/dueBefore/description-search cases as the SQLite integration test, plus
   that the label filter leaves the LATERAL-aggregated label list intact. All passed, against a throwaway
   `tickethub_filter_test_*` database, dropped afterward.
5. Standalone Playwright/Chromium script, against a locally running server, SQLite, demo-seeded: verified
   each new filter control in isolation (type=bug, priority=highest, assignee=alex, label=backend,
   description-only search text) narrows the Issues table to exactly the expected row(s); verified
   "Clear" restores the full unfiltered list; verified a combined project+type filter narrows correctly;
   verified the Board view does not inherit a filter left set on the Issues view. Also directly confirmed
   the default project preselection on first opening the Issues view is pre-existing behavior (unrelated
   to this batch) and not a filter-widening regression, and that an empty-result table's "No issues
   found" placeholder row was the source of an initial miscount in a throwaway debug script, not an
   application bug.
6. Re-ran the markdown, mentions/notifications, comment-reaction, worklog, audit-log, and comment
   edit/delete browser tests against the same build to confirm no regression -- all still pass unchanged.

All checks passed. No committed test scripts or screenshots (scratchpad only).

### What is still not built

Phase 5's remaining items: dashboard personalization (D24), Kanban board WIP limits and drag-and-drop
(D33 plus the roadmap's own "usable end-to-end" exit-gate wording), and the full attachments vertical
(D15/D98-D105) -- upload API, local filesystem storage, native-element previews, recycle bin, retention,
and Markdown-editor integration. All untouched by this batch.

## 2026-07-31 — Simple append-only admin/security audit log (D23): Phase 4 complete

Sixth and final Phase 4 (Collaboration) slice. With this batch, every item in
`docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 4 list is implemented.

### What changed

- Migration `012_audit_log.sql` (both backends) adds `audit_events` (`id`, `category`, `action`,
  `actor_user_id` nullable `ON DELETE SET NULL`, `target_type`/`target_id` nullable, `details` nullable,
  `created_at`) -- a simplified version of the original baseline schema (`docs/DATA_MODEL.md`): no
  `actor_type`/`project_id`/`ip_address`/`correlation_id`/`before_json`/`after_json`/`metadata_json`
  columns, and no separate `audit_retention_policies` table. Rows are appended and never updated or
  purged, matching D23's "no categories[-as-a-retention-feature], export, or configurable retention"
  simplification.
- `Domain::AuditEvent`; `IDatabase::recordAuditEvent` (fire-and-forget, returns `void` -- unlike
  `createNotification`, whose result the notification list feature reads back immediately) and
  `listAuditEvents(limit)` (newest-first, no pagination/filtering) in both adapters.
- Wired into a small, deliberately focused set of existing call sites rather than a general-purpose
  audit hook on every write: `AuthService::login` on a wrong password (`auth`/`login.failed`) or an
  attempt against an already-locked account (`auth`/`login.blocked`); `AuthService::createUser`
  (`identity`/`user.created`, with no actor since `ticket-hub-cli create-user` runs outside any web
  session); `TicketService::setAnonymousReadEnabled`/`permanentlyDeleteProject`/`permanentlyDeleteIssue`
  (all `admin`-category, attributed to the acting global administrator).
- `TicketService::listAuditEvents` (global-administrator-only, same level as the recycle bins) and a new
  `GET /api/admin/audit-events` route.
- `web/`: a new "Audit log" nav item, hidden by default and shown only for global admins
  (`renderCurrentUser`), and re-hidden on logout (`showLoginScreen`) so it can't leak to whoever logs in
  next in the same browser tab. Renders a simple read-only table (when, category, action, actor, target,
  details) via a new `renderAuditLog()` view, reusing the existing `.issue-table`/`.panel` styles rather
  than introducing new CSS.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 7/7 green, including new assertions in `sqlite_integration_tests`
   (`recordAuditEvent`/`listAuditEvents` round-trip with and without an actor, newest-first ordering,
   the `limit` parameter capping the result size), `identity_integration_tests` (a failed login records
   `auth`/`login.failed`, a blocked login records `auth`/`login.blocked`, a CLI-driven `createUser`
   records `identity`/`user.created` with no actor), and `authorization_integration_tests`
   (`listAuditEvents` is global-administrator-only; `permanentlyDeleteIssue`, `setAnonymousReadEnabled`,
   and `permanentlyDeleteProject` each produce their expected event, all correctly attributed to the
   actor who performed the action).
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile cleanly.
4. Live PostgreSQL verification: a standalone smoke-test program (not committed) exercised
   `recordAuditEvent`/`listAuditEvents` directly against `PostgresDatabase`, then separately confirmed
   (via the same live database) that a `ticket-hub-cli create-user` invocation against that same database
   -- run through the actual CLI binary, not a direct database call -- had already produced an
   `identity`/`user.created` audit row with no actor. All passed.
5. Standalone Playwright/Chromium script, against a locally running server, SQLite, demo-seeded: logged
   in as `sam` (non-admin) and confirmed the "Audit log" nav item is not visible; logged out and
   confirmed the nav item is hidden again (not just for the just-logged-out user, but as a state reset
   before the next login); logged in as `demo` (global admin) and confirmed the nav item is visible;
   toggled the anonymous-read-access setting on and off via two direct API calls (to generate two
   deterministic events without depending on incidental audit activity elsewhere); opened the Audit log
   view and confirmed at least two rows appeared, with the first row's text containing both
   `settings.anonymous_read_changed` and the actor's display name ("Demo User").
6. Re-ran the reaction, comment-editing, mentions/notifications, and worklog browser tests against the
   same build to confirm no regression -- all still pass unchanged.

All checks passed. No committed test scripts or screenshots (scratchpad only).

### What is still not built

Phase 4 (Collaboration) is now fully implemented. Phase 5 (attachments and the Kanban board) is
untouched and is the natural next step. The audit log itself has no export, filtering beyond the fixed
200-row cap, or configurable retention -- not a gap, but D23's explicit V1 scope.

## 2026-07-31 — Simplified worklogs (D12/D13)

Fifth Phase 4 (Collaboration) slice, and the last one before D23 (audit log) closes out the phase: issues
can now have time logged against them.

### What changed

- Migration `011_worklogs.sql` (both backends) adds `worklogs` (`id`, `issue_id`, `author_user_id`,
  `work_date`, `time_spent_seconds`, `comment` nullable, `deleted_at`/`deleted_by_user_id`, `created_at`,
  `updated_at`, `version`) -- no `remaining_adjustment_mode`/`remaining_estimate_seconds_after` columns
  from the original baseline schema, since D12 dropped time estimates from V1 entirely.
- `Domain::Worklog`/`AddWorklogRequest`/`EditWorklogRequest`; `Domain::validateAddWorklog`/
  `validateEditWorklog` (workDate required, timeSpentSeconds in `(0, 3600000]` seconds, comment ≤ 10000
  chars).
- `IDatabase::listWorklogs`/`addWorklog`/`findWorklogById`/`editWorklog`/`deleteWorklog` in both
  adapters, mirroring the comment CRUD implementation exactly: `editWorklog` shares `editComment`/
  `editIssue`'s optimistic-locking contract (`expectedVersion` -> `Domain::ConcurrencyConflict`);
  `deleteWorklog` is a tombstone delete.
- `TicketService::addWorklog`/`editWorklog`/`deleteWorklog`: deliberately drop D83's author-or-admin
  permission split. All three require only project-Member-or-above on the issue's project -- the same
  level as any other issue write -- so any project member may edit or delete *any* worklog on an issue
  they can access, not just the one they logged themselves. This is D13's explicit simplification
  ("no separate own-vs-others edit/delete permission split"), and is deliberately more permissive than
  comments.
- `Api.cpp`: new `GET`/`POST /api/issues/{key}/worklogs` and `PATCH`/`DELETE
  /api/issues/{key}/worklogs/{id}` routes, following the established auth/CSRF/error-mapping pattern.
- `web/`: a "Time tracking" section in the issue drawer. `formatDuration`/`parseDurationToSeconds`
  convert between a plain integer of seconds and a free-text "1h 30m" style duration. Each logged entry
  shows a Delete button unconditionally (no client-side author check, since the server itself allows any
  project member to delete any entry -- unlike comments, where the client hides Edit/Delete for
  non-authors as a UX simplification). The log-time form's duration field has an HTML5 `pattern`
  attribute as a first line of defense against malformed input, plus a JS-level parse-and-toast fallback.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 7/7 green, including new assertions in `sqlite_integration_tests` (full
   CRUD round-trip, version increment on edit, stale-version conflict, tombstone semantics -- excluded
   from listing/lookup but the row still physically present) and `authorization_integration_tests` (a
   non-member rejected on another project's issue; a different project member editing and then deleting
   someone else's worklog both succeed, explicitly asserting the no-own-vs-others-split behavior; editing
   an unknown worklog id still respects the project-role check before the not-found path; unknown-worklog
   edit/delete return nullopt/false rather than throwing).
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile cleanly.
4. Live PostgreSQL verification: a standalone smoke-test program (not committed) exercised `addWorklog`/
   `listWorklogs`/`findWorklogById`/`editWorklog` (including the stale-version-conflict rejection) and
   `deleteWorklog` directly against `PostgresDatabase` and a local PostgreSQL 16 server -- all passed.
5. Standalone Playwright/Chromium script, against a locally running server, SQLite, demo-seeded: opened
   an issue with no logged time and confirmed the empty state; logged "1h 30m" with a comment and
   confirmed the row shows both the correctly-formatted duration and the comment text; deleted the entry
   and confirmed the list returns to empty; submitted a garbage duration value and confirmed no new
   worklog row appeared -- the input's HTML5 `pattern` attribute blocks the browser's native form
   submission before the JS `submit` handler (and its `parseDurationToSeconds` fallback) ever fires, so
   this particular check exercised the HTML5-level guard, not the JS-level one; the JS fallback exists
   for completeness (e.g. a submission path that bypasses native validation) but was not itself
   triggered by this check.

All checks passed. No committed test scripts or screenshots (scratchpad only).

### What is still not built

Only D23 (the append-only admin/security audit log) remains open in Phase 4. Phase 5 (attachments and
the Kanban board) is untouched. There is no worklog totals/summary view (e.g. "time logged this week"),
just the per-issue list -- not called for by D12/D13's simplification.

## 2026-07-31 — Markdown editor toolbar, live preview, and sanitized rendering (D16)

Fourth Phase 4 (Collaboration) slice: comment bodies and issue descriptions now render as formatted
Markdown, with a visual toolbar and a live preview toggle. Entirely a `web/` change -- no schema or API
change, since bodies are still stored/transmitted as raw Markdown text.

### What changed

- `renderMarkdownInline`/`renderMarkdown`: a deliberately small Markdown-to-HTML subset -- bold
  (`**x**`), italic (`*x*`), inline code, links, `#`/`##`/`###` headings, `-`/`*` and `1.` lists, `>`
  blockquotes, fenced code blocks, `---`/`***` horizontal rules. Safe by construction: the raw text is
  HTML-escaped *first* (the same `escapeHtml` used everywhere else in `web/`), and every subsequent
  transform only wraps the already-escaped text in a fixed, hardcoded set of tags -- user input can never
  introduce a real HTML tag or attribute this way, so there is no separate sanitization pass that could be
  wrong. Link targets are restricted to `http(s)`/`mailto` via a scheme check on the captured URL; any
  other scheme is left as literal `[text](url)` text rather than becoming a clickable anchor.
- `attachMarkdownToolbar`: Bold/Italic/Code/Link/Bulleted-list/Numbered-list/Quote buttons plus a
  Preview toggle, wired to all four Markdown-capable textareas -- comment add, comment edit, issue
  description on create, issue description on edit. Pure `textarea.selectionStart`/`selectionEnd`
  manipulation (wrap-selection for inline styles, prefix-each-line for block styles), no
  `execCommand`/`contenteditable`, so the field stays a real `<textarea>` and every existing
  FormData-based submit handler and the @mention autocomplete (which reads `selectionStart` directly)
  keep working unchanged.
- `.comment-body-text` changed from a `<p>` to a `<div class="markdown-body">` (a `<p>` cannot legally
  contain block-level children like `<ul>`/`<blockquote>`/`<h2>`, which `renderMarkdown` can now produce).
  The issue drawer's Description section renders through the same path.

### A rendering bug caught and fixed during implementation

The first cut of the italic regex accepted `_..._` as an alternative to `*...*` (matching the common
Markdown convention of supporting both delimiters for both bold and italic). This mishandled any text
containing two separate double-underscore identifiers -- e.g. two `__dunder__`-style names, or in the
XSS test payload below, `window.__xssFired` appearing twice -- because the regex engine matched an
underscore from the *first* pair as an opening delimiter and an underscore from the *second* pair as the
closing one, silently swallowing everything in between (including other underscore-delimited content)
into a single `<em>` span. The output remained safely escaped throughout (this was never an XSS risk,
just visually wrong), but it was still a real correctness bug. Caught by a Playwright assertion that
expected specific literal text to be present in a comment and found it missing, split across an
unexpected `<em>` boundary. Fixed by dropping underscore-delimited emphasis entirely -- bold and italic
now use only `**`/`*`, which have no adjacency ambiguity since a bare `*` can never be confused with a
`**` pair, and there's no snake_case-style convention for single asterisks the way there is for single
underscores.

### Verification

1. `node --check web/app.js`: no syntax errors (this batch made no C++ changes, so no `cmake --build`
   was needed for the feature itself; `ctest --output-on-failure` was still re-run to confirm no
   unrelated regression -- 7/7 green, unchanged from the previous batch).
2. Standalone Playwright/Chromium script, against a locally running server, SQLite, demo-seeded:
   - Toolbar: typed "hello world", selected "world", clicked the Bold button, confirmed the textarea
     value became `hello **world**`.
   - Preview: clicked the Preview toggle, confirmed the preview pane's HTML contains `<strong>world</strong>`,
     confirmed the textarea itself is hidden while previewing, then toggled back and confirmed the
     textarea is visible again.
   - Posted a comment containing bold/italic/inline-code/a link/a bulleted list/a blockquote, and
     confirmed the rendered comment contains exactly the expected `<strong>`/`<em>`/`<code>`/`<a href=
     "https://example.com">`/`<ul><li>`/`<blockquote>` elements (not literal asterisks/brackets).
   - **Security check 1**: posted a comment whose entire body was
     `<script>window.__xssFired = true;</script> and <img src=x onerror="window.__xssFired = true">`.
     Confirmed via `page.evaluate` that `window.__xssFired` was never set to `true` (the payload never
     executed), and confirmed the literal text is visible in the rendered comment (proving it was
     escaped and displayed, not silently dropped).
   - **Security check 2**: posted a comment `[click me](javascript:alert(1))` and confirmed the rendered
     comment contains zero `<a>` elements -- the `javascript:` scheme was rejected and the text rendered
     as literal `[click me](javascript:alert(1))`, not a clickable link.
   - Edited an issue's description to `## Heading\n\nSome **bold** description text.` and confirmed the
     rendered description contains both an `<h2>` and a `<strong>`.
3. Re-ran the eighth batch's reaction browser test, the seventh batch's comment-editing browser test, and
   the ninth batch's mentions/notifications browser test against the same build to confirm no regression
   from the `.comment-body-text` markup change (`<p>` to `<div>`) or the new toolbar/preview DOM elements
   -- all three still pass unchanged.

All checks passed (after the underscore-emphasis bug above was found and fixed). No committed test
scripts or screenshots (scratchpad only).

### What is still not built

The rest of Phase 4 remains unimplemented: simplified worklogs (D13) and the append-only admin/security
audit log (D23). Phase 5 (attachments and the Kanban board) is untouched. The Markdown renderer is
intentionally a small subset -- no tables, no nested lists, no strikethrough, no images -- consistent
with "a deliberately small subset, not a general-purpose engine" rather than a gap to fill later.

## 2026-07-31 — @mention handles and the fixed in-app notification set (D56/D80/D14)

Third Phase 4 (Collaboration) slice: users can now have an optional, unique @mention handle; comments are
scanned for `@handle` mentions at creation; and three fixed notification types (assigned, mentioned,
comment on a watched issue) are created as a side effect of existing writes and surfaced through a new
notification bell in the UI.

### What changed

- Migration `010_mentions_and_notifications.sql` (both backends) adds `users.handle` (nullable, unique
  via a partial index -- SQLite's `ALTER TABLE ADD COLUMN` cannot itself carry a `UNIQUE` constraint) and
  `notifications` (`id`, `user_id`, `type` CHECK-constrained to `assigned`/`mentioned`/`watched_comment`,
  `issue_id` nullable `ON DELETE CASCADE`, `read_at` nullable, `created_at`) -- the exact minimal shape
  documented in `docs/REDUCED_SCOPE_DATA_MODEL.md`.
- `Domain::User`/`CreateUserRequest` gained `handle`; `Domain::Notification` and the three fixed
  `NotificationType*` constants; `Domain::normalizeHandle`/`isValidHandle` (lowercase, 1-32
  letters/digits/underscores, same normalization style as email).
- `IDatabase` gained `findUserByHandle` and `createNotification`/`listNotifications`/
  `countUnreadNotifications`/`markNotificationRead`/`markAllNotificationsRead`, implemented in both
  adapters. `listNotifications`/`createNotification` resolve `issueKey`/`issueSummary` at read time via a
  `LEFT JOIN` on `issues`, since there is no stored message string.
- `AuthService::createUser` normalizes and pre-checks handle uniqueness the same way it already does for
  email (not relying on the database constraint's error message); `ticket-hub-cli create-user` gained
  `--handle=<handle>`. The three seeded demo users (`002_seed_demo.sql`, both backends) now have handles
  `demo`/`alex`/`sam`.
- `TicketService::createIssue`/`editIssue` compare the issue's assignee before and after the write and
  notify a newly-set or changed assignee -- skipping self-assignment and a no-op re-save with the same
  assignee. `TicketService::addComment` extracts every distinct `@handle` token from the comment body
  with a plain regex (once, at creation -- `editComment` does not re-scan, to avoid re-notifying on every
  save of an already-mentioning comment), resolves each against `findUserByHandle`, and notifies each
  resolved user (other than the comment's own author) plus every current watcher of the issue (other than
  the author) -- deduplicated so a recipient who is both mentioned and watching the same comment gets
  exactly one notification, "mentioned" winning over the generic "watched_comment".
- `Api.cpp`: new `GET /api/users` (a slim directory listing -- id/displayName/email/handle only --
  requiring a session even when anonymous read is on, since the user directory is more sensitive than
  issue data) and `GET /api/notifications[?unread=true]`, `GET /api/notifications/unread-count`,
  `POST /api/notifications/{id}/read`, `POST /api/notifications/read-all`, all scoped to the caller's own
  notifications. Also removed a stale "this file could not be compiled" comment at the top of `Api.cpp`
  left over from before the server target was first built and verified this session -- no longer
  accurate and actively misleading.
- `web/`: a notification bell with an unread-count badge in the top bar (`loadBaseData()` fetches the
  count once per login/init); clicking it opens a panel listing every notification (`renderNotificationPanel`),
  each clickable to mark it read and open the related issue, plus a "mark all read" button. An @mention
  autocomplete dropdown (`attachMentionAutocomplete`) is attached to both the add-comment textarea and the
  edit-comment textarea: typing `@partial` shows up to 5 matching handles from the cached `/api/users`
  directory (fetched once in `loadBaseData()`, no per-keystroke network request), and clicking a
  suggestion replaces the partial token with the full `@handle `.

### A design correction caught while writing the authorization test

The first draft of the notification authorization test accumulated state on a single shared issue across
sub-tests (assign, then reassign, then comment-mention, then watch, then comment-again), asserting
absolute unread counts at each step. This broke as soon as a later sub-test's comment triggered a
`watched_comment` notification for a user still watching from an earlier sub-test -- an interaction the
absolute-count assertions hadn't accounted for. Caught immediately by a failing assertion, not by
production behavior being wrong. Fixed by giving each notification type (`assigned`, `mentioned`,
`watched_comment`, and the mentioned-wins-over-watched dedupe) its own dedicated issue and calling
`markAllNotificationsRead` as an explicit reset between sub-tests, so no sub-test's side effects can leak
into the next one's assertions.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 7/7 green, including new assertions in `sqlite_integration_tests`
   (`findUserByHandle` resolving/rejecting, `createNotification` resolving the issue key, `listNotifications`/
   `countUnreadNotifications` with the `unreadOnly` filter, `markNotificationRead`/`markAllNotificationsRead`
   idempotency and per-user scoping), `identity_integration_tests` (handle lowercased on create, a
   duplicate handle rejected even with different casing, an invalid handle format rejected, the handle
   remaining optional), and `authorization_integration_tests` (assigned/self-assigned/unchanged-reassign,
   mentioned/unknown-handle/self-mention, watched-comment/self-watch-self-comment, the
   mentioned-wins-over-watched dedupe, `listUsers` returning the seeded handles, and per-user scoping of
   mark-read/mark-all-read).
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile cleanly.
4. Live PostgreSQL verification: `ticket-hub-cli create-user ... --handle=extra_user` against a local
   PostgreSQL 16 server, followed by a standalone smoke-test program (not committed) exercising
   `findUserByHandle` and all five notification methods directly against `PostgresDatabase` -- all passed.
5. Standalone Playwright/Chromium script, against a locally running server, SQLite, demo-seeded: logged in
   as `demo`, created an issue assigned to `alex` (auto-opens its drawer), typed `@sa` in the comment box
   and confirmed a "@sam — Sam Lee" autocomplete suggestion appeared and, on click, inserted `@sam ` into
   the textarea; posted the comment. Logged out, logged in as `alex`: unread badge read "1"; opened the
   notification panel, confirmed the single item's type was "assigned"; clicking it opened the issue
   drawer and marked it read (badge went to hidden/0). Logged out, logged in as `sam`: unread badge read
   "1"; the notification's type was "mentioned" and its summary referenced the correct issue key;
   "mark all read" cleared the badge. Re-ran with a second, isolated instance of the same script against a
   freshly reset database to rule out cross-run state contamination, confirming identical results.
6. Re-ran the eighth batch's reaction browser test and the seventh batch's comment-editing browser test
   against the same build to confirm no regression from the new notification-bell markup and mention
   autocomplete wrapper elements -- both still pass unchanged.

All checks passed. No committed test scripts or screenshots (scratchpad only).

### What is still not built

The rest of Phase 4 remains unimplemented: the full Markdown editor/toolbar/preview (D16), simplified
worklogs (D13), and the append-only admin/security audit log (D23). Phase 5 (attachments and the Kanban
board) is untouched. There is still no self-service profile editing -- a handle can only be set at
account creation via the CLI, not changed afterward.

## 2026-07-31 — Fixed emoji reactions on comments (D84)

Second Phase 4 (Collaboration) slice: comments can now receive a fixed set of emoji reactions, mirroring
the existing self-service watch/vote pattern (D20/D79).

### What changed

- Migration `009_comment_reactions.sql` (both backends) adds `comment_reactions`: a three-column
  composite-primary-key many-to-many table (`comment_id`, `user_id`, `reaction_key`), `ON DELETE CASCADE`
  on both foreign keys, `reaction_key` constrained (`CHECK`) to a fixed eight-value set. The decision
  register (D84) calls for "a fixed reaction set" without naming one, so this uses GitHub's own
  well-known set (`thumbs_up`, `thumbs_down`, `laugh`, `hooray`, `confused`, `heart`, `rocket`, `eyes`) as
  a conservative, familiar default -- a filled product-decision gap, documented here and in `docs/SCHEMA.md`.
- `Domain::CommentReaction` (`reactionKey` + `UserSummary user`) and `Domain::isValidCommentReactionKey`.
- `IDatabase` gained `addCommentReaction`/`removeCommentReaction` (idempotent: return `true` only when a
  row was actually inserted/removed, matching `watchIssue`/`voteIssue`) and `listCommentReactions`
  (returns the raw `(reactionKey, user)` rows for a comment). Implemented in both `SqliteDatabase` and
  `PostgresDatabase`.
- `TicketService::addCommentReaction`/`removeCommentReaction`/`listCommentReactions`: no project-role
  check, same reasoning as watch/vote -- any authenticated user may react to any comment. Unlike
  `editComment`/`deleteComment`'s nullopt/false not-found convention, an unknown issue, comment, or
  reaction key throws `std::invalid_argument`, matching `watchIssue`'s own unknown-issue behavior (chosen
  deliberately so the add/remove boolean can keep meaning "idempotent no-op" without being overloaded
  with a second, conflicting "not found" meaning -- see the design note below).
- `Api.cpp`: new `GET /api/issues/{key}/comments/{id}/reactions` and
  `POST`/`DELETE /api/issues/{key}/comments/{id}/reactions/{key}` routes, following the established
  auth/CSRF/error-mapping pattern. The POST/DELETE handlers ignore the service-layer boolean entirely
  (mirroring the existing watch/vote route handlers) and always return `{ok: true}` on success, since a
  not-found now throws instead.
- `web/app.js`: a `COMMENT_REACTIONS` catalog (key + emoji) drives a row of eight pill buttons under each
  comment, fetched via one reactions request per comment alongside the existing per-issue fetches. Each
  button shows a live count and highlights (`.reaction-button--active`) when the current viewer has that
  reaction; clicking POSTs or DELETEs depending on the button's current state and re-renders. `web/styles.css`
  gained `.comment-reactions`/`.reaction-button`/`.reaction-button--active`.

### A design correction caught during implementation

The first cut of `TicketService::addCommentReaction`/`removeCommentReaction` returned `false` for "the
comment does not exist" -- copying `editComment`/`deleteComment`'s convention -- while also trying to
return the database layer's own idempotency signal (`true` only if a row was newly inserted/removed) on
the success path. Those two conflate into one `bool`: a `false` could mean either "already reacted,
harmless" or "comment does not exist, and reacting silently failed" and the caller could not tell which.
Caught by a newly written test (`reacting again with the same key is a no-op` failed after the return
value was collapsed to always `true` on success) before this ever reached the API layer. Fixed by making
unknown-issue/comment/key cases throw `std::invalid_argument` instead of returning `false`, freeing the
boolean to mean only "idempotent no-op" -- consistent with `watchIssue`'s own behavior, which already
throws for an unknown issue key rather than returning `false`.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files.
2. `ctest --output-on-failure`: 7/7 green, including new assertions in `sqlite_integration_tests`
   (react/react-again-no-op/second-user-same-key/second-key-same-user, `listCommentReactions` count,
   remove/remove-again-no-op, remaining-count-after-remove) and `authorization_integration_tests`
   (a non-project-member can still react, `listCommentReactions` visible to any authenticated reader,
   removing one's own reaction, an unknown reaction key rejected, reacting/un-reacting to an unknown
   comment rejected).
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile cleanly (SQLite-only
   naturally excludes the SQLite-specific integration/identity/authorization/workflow test binaries when
   SQLite itself is off, as already documented; PostgreSQL-only compiles the core/CLI/domain/migration/
   crypto targets).
4. Live PostgreSQL verification: a standalone smoke-test program (not committed) exercised
   `addCommentReaction`/`removeCommentReaction`/`listCommentReactions` directly against
   `PostgresDatabase` and a local PostgreSQL 16 server (created and dropped for this check), alongside a
   re-check of `findCommentById`/`editComment` from the prior batch -- all passed.
5. Standalone Playwright/Chromium script, against a locally running server, SQLite, demo-seeded: logged
   in as `demo`, added a comment, confirmed all eight reaction buttons render; clicked `thumbs_up` and
   confirmed it goes active with a "1" count; clicked it again and confirmed it reverts to inactive with
   no count; clicked `heart` and confirmed it can be active alongside the (now inactive) `thumbs_up`
   button. Logged out, logged in as `alex`, opened the same issue, and confirmed the `heart` count reads
   "1" (shared state, correct before alex has reacted); clicked `heart` as alex and confirmed the count
   becomes "2". A second, focused script explicitly asserted the `reaction-button--active` CSS class
   (not just the visible count) before and after alex's own click, confirming alex's own highlight starts
   `false` (only demo had reacted) and flips to `true` only after alex's own click -- the count and the
   per-viewer highlight are tracked independently, as intended.
6. Re-ran the full comment-editing/tombstone-delete browser test from the previous batch against the same
   build to confirm no regression from the new reaction markup inside each comment -- unchanged pass.

All checks passed. No committed test scripts or screenshots (scratchpad only).

### What is still not built

The rest of Phase 4 remains unimplemented: the full Markdown editor/toolbar/preview (D16), `@handle`
mentions with autocomplete (D80, needs a new `users.handle` column, D56), the fixed in-app notification
set (D14), simplified worklogs (D13), and the append-only admin/security audit log (D23). Phase 5
(attachments and the Kanban board) is untouched.

## 2026-07-31 — Comment editing and tombstone delete (D81/D82/D83): Phase 4 started

First Phase 4 (Collaboration) slice: comments can now be edited (with an `edited_at` marker, D81) and
tombstone-deleted (soft delete via the same columns issues/projects already use, D82), with simplified
author-or-project-admin permissions (D83).

### What changed

- Migration `008_comment_editing.sql` (both backends) adds `comments.edited_at` (`TEXT` on SQLite,
  `TIMESTAMPTZ` on PostgreSQL).
- `Domain::Comment` gained `editedAt` (`std::optional<std::string>`).
- `IDatabase` gained `findCommentById`, `editComment` (same optimistic-locking contract as `editIssue`:
  a mismatched `expectedVersion` throws `Domain::ConcurrencyConflict`; sets `edited_at` and bumps
  `version` on success), and `deleteComment` (tombstone soft-delete, mirrors `softDeleteIssue` exactly).
  Implemented in both `SqliteDatabase` and `PostgresDatabase`, sharing a `readComment`/`CommentSelect`
  helper with the existing `listComments`/`addComment` (which were rewritten to use it instead of
  duplicating the column list).
- `TicketService` gained `editComment`/`deleteComment`: the comment's own author may always act on it;
  otherwise the actor needs project-Admin-or-above on the comment's issue's project (checked via the
  existing `requireProjectRole`, which already bypasses for global admins -- no separate check needed).
- `Api.cpp`: `commentJson` now includes `version`/`editedAt`; new `PATCH`/
  `DELETE /api/issues/{key}/comments/{id}` routes follow the established auth/CSRF/error-mapping pattern
  (`Domain::ConcurrencyConflict` -> 409, `Domain::Forbidden` -> 403, `std::invalid_argument` -> 400).
- `web/app.js`: comment list rendering gained `data-comment-id`, an `(edited)` marker, and conditional
  Edit/Delete buttons (`canModerate = comment.author.id === state.principal?.userId ||
  state.principal?.isAdmin` -- a client-side simplification, not the security boundary, since the client
  never loads per-project role information anywhere else in the app either; the server enforces D83
  independently). Edit toggles an in-place textarea (Save sends `PATCH` with `expectedVersion:
  comment.version`; Cancel discards by re-fetching the issue). Delete sends `DELETE` and refreshes.
  `web/styles.css` gained `.comment-actions`/`.comment-edit-textarea`/`.comment-edit-actions`.

### Verification

1. `cmake --build` (full config, `-DTICKETHUB_BUILD_SERVER=ON`): zero warnings/errors from Ticket Hub's
   own files (Crow's headers still emit their own expected `-Wconversion` noise).
2. `ctest --output-on-failure`: 7/7 green, including new assertions in `sqlite_integration_tests`
   (`findCommentById`, `editComment` success/version-increment/`edited_at`-set/stale-conflict/
   unknown-returns-nullopt, `deleteComment` soft-delete/excluded-from-listing/not-found/no-op-on-repeat/
   row-still-physically-present) and `authorization_integration_tests` (non-author-non-admin Forbidden
   for edit and delete, self-edit succeeds, global-admin can edit/delete any comment, unknown-comment
   returns nullopt/false).
3. Re-ran the SQLite-only and PostgreSQL-only build configurations: both compile and pass cleanly.
4. Live PostgreSQL verification: a standalone smoke-test program (not committed) exercised
   `findCommentById`/`editComment`/`deleteComment` directly against `PostgresDatabase` and a local
   PostgreSQL server (created and dropped for this check), confirming the same behavior as the SQLite
   integration tests.
5. Standalone Playwright/Chromium script (same approach as every prior UI batch), against a locally
   running server, SQLite, demo-seeded: logged in as `demo`, added a comment, confirmed the Edit button
   is visible for the author's own comment, edited it and confirmed the new body and an `(edited)`
   marker appear, clicked Edit again and Cancel and confirmed the change was discarded, then deleted the
   comment and confirmed the comment count dropped by one. Logged out, logged in as `alex`, added a
   second comment; logged out, logged in as `sam` (neither the author nor a global admin), opened the
   same issue, and confirmed `sam` sees zero Edit buttons on `alex`'s comment (client-side hiding,
   backed by the server-side authorization tests above).
6. Re-ran every prior UI batch's browser test (login, hierarchy/resolution pickers, edit/links/clone/
   watch-vote, project management, both recycle bins, reorder/move/bulk actions) against the same build
   to confirm no regression -- all still pass unchanged.

All checks passed. No committed test scripts or screenshots (scratchpad only).

### What is still not built

The rest of Phase 4 remains unimplemented: the full Markdown editor/toolbar/preview (D16), fixed emoji
reactions on comments (D84), `@handle` mentions with autocomplete (D80, needs a new `users.handle`
column, D56), the fixed in-app notification set (D14), simplified worklogs (D13), and the append-only
admin/security audit log (D23). Phase 5 (attachments and the Kanban board) is untouched.

## 2026-07-31 — Reorder, move, and bulk-action UI: `web/` now covers every Phase 1-3 route

Added the last three missing pieces identified in `NEXT.md`: manual ordering (D31), moving an issue
between projects (D37), and simple bulk actions (D36). With this batch, every write route added across
Phases 1-3 has a reachable control somewhere in the demo UI.

### What changed

- `web/app.js`: `issueRows()` gained `orderable` (adds an Order column with move-up/move-down buttons,
  disabled at the ends) and `selectable` (adds a checkbox column) options. `renderIssuesView()` now sorts
  the fetched issues by `rankOrder` and passes `orderable: true` whenever exactly one project is selected
  (reordering is inherently project-scoped -- `reorderIssue`'s `beforeIssueKey` anchor must be in the same
  project); always passes `selectable: true` in the active (non-recycle-bin) view and renders a bulk-action
  bar that appears once at least one checkbox is checked.
- The issue drawer gained a "Move to project" control (target-project `<select>` + button, omitted
  entirely when there is nowhere else to move to) calling `POST /api/issues/{key}/move`; on success it
  reopens the drawer under the issue's new key.
- `deletedIssueRows()` (from the previous batch) is unaffected -- reorder/select only apply to the active
  view, matching that soft-deleted issues can't be reordered or bulk-acted-on anyway.

### A bug caught and fixed before it could surface in testing

The new checkboxes and move-up/move-down buttons render inside the same `<tr>` that `bindIssueLinks()`
already binds a click-to-open-drawer handler to (unchanged from every prior batch). Recognized during
implementation, before running the browser test, that clicking a checkbox or a reorder button would
therefore *also* open the issue drawer via the bubbled click event -- fixed proactively by calling
`event.stopPropagation()` in both the checkbox and the move-up/move-down click handlers, then confirmed
with an explicit assertion in the browser test (`drawer NOT opened by checkbox click: true`) rather than
just trusting the fix.

### Verification

Standalone Playwright/Chromium scripts (same approach as prior UI batches), against a locally running
server, SQLite, demo-seeded:

1. Filtering the Issues table to project `TH` shows an Order column; filtering back to "All projects"
   hides it (count 0).
2. Moving the second row up swaps it with the first row exactly (`[TH-2, TH-1, TH-3, ...]`); moving the
   (now-first) row back down restores the original order (`[TH-1, TH-2, TH-3, ...]`) -- confirms the
   move-up/move-down anchor math is correct in both directions, not just one.
3. Opening `TH-4`'s drawer shows the "Move to project" control; moving it to `WEB` reopens the drawer
   showing `WEB-3` (the newly allocated key).
4. The bulk-action bar is hidden with zero rows checked; checking two rows shows "2 selected" and does
   *not* open the drawer (the proactive fix, explicitly asserted); bulk-adding a label to both reports "2
   succeeded, 0 failed" as a toast.
5. Re-ran every prior UI batch's browser test (login, hierarchy/resolution pickers, edit/links/clone/
   watch-vote, project management, issue recycle bin) against the same build to confirm no regression --
   all five still pass unchanged.

All checks passed on the first full run (the checkbox/click-bubbling issue was caught by reasoning through
the DOM structure during implementation and fixed before ever running the test, not found by a failing
test). No committed test files or screenshots (scratchpad only).

### What is still not built

Nothing from the Phase 1-3 API surface is missing UI coverage anymore. `web/` still has no UI for
anything beyond that surface (e.g. no drag-and-drop on the Board, no keyboard-driven bulk selection) --
those were never part of the reduced-scope V1 target's write-route list, just possible UX refinements.

## 2026-07-31 — Issue recycle bin UI added

Added the symmetric counterpart to last batch's project recycle bin: a Delete action in the issue drawer
and a recycle-bin view in the Issues list, both gated the same way (global-administrator-only to view the
bin/restore/purge; project-Admin-or-above to soft-delete, matching D22's mirror of D88/D89).

### What changed

- `web/app.js`: a Delete button in the drawer's actions row (`DELETE /api/issues/{key}`); `renderIssues()`
  now delegates to `renderIssuesView(showingDeleted)` exactly mirroring `renderProjectsView`'s shape --
  active view keeps the existing filter bar, recycle-bin view replaces it with Restore/Delete-permanently
  buttons per row from `GET /api/issues/deleted`, and a toggle button visible only to global admins swaps
  between the two.
- Deleted-issue rows are deliberately not given a click-to-open-drawer handler, since a soft-deleted issue
  is excluded from ordinary `GET /api/issues/{key}` lookup (would 404).

### Verification

Standalone Playwright/Chromium scripts (same approach as prior UI batches), against a locally running
server, SQLite, demo-seeded:

1. As the global-admin `demo` user: deleting `TH-1` via the drawer's Delete button closes the drawer and
   removes the row from the active Issues table; the recycle-bin toggle shows it in the bin; Restore
   returns it to the active table; deleting it again and clicking Delete-permanently removes it from the
   bin entirely.
2. As `sam` (a TH project member, not a project admin): the recycle-bin toggle is entirely absent from the
   Issues view; attempting to delete `TH-2` via the drawer fails with the server's exact message ("Actor
   lacks the required role on project TH") shown as a toast, and the drawer stays open since the delete
   never actually happened -- confirming the client doesn't fake success or crash on a 403.
3. Re-ran all four prior UI batches' browser tests (login, hierarchy/resolution pickers, edit/links/clone/
   watch-vote, project management) against the same build to confirm no regression -- all still pass
   unchanged.

All checks passed on the first implementation attempt (after fixing one ambiguous test locator, not an
app bug -- an `h1` selector matched both the recycle-bin page heading and the still-in-DOM, merely hidden,
issue drawer's own `<h1>`). No committed test files or screenshots (scratchpad only).

### What is still not built

Bulk actions and reorder/move still have no UI in `web/` -- see `NEXT.md`.

## 2026-07-31 — Project-management UI added: create, archive, recycle bin

Added the next missing `web/` area: project lifecycle management. Previously the Projects view was purely
read-only (a grid of cards, click-to-open-board) with no way to create, archive, delete, restore, or
permanently purge a project from the UI at all.

### What changed

- `web/index.html`: a `#project-modal` (key/name/description) matching the create-issue modal's pattern.
- `web/app.js`: `renderProjects()` now delegates to `renderProjectsView(showingDeleted)`, which renders
  either the active project grid (with Archive/Unarchive and Delete buttons per card, plus a "New project"
  button, plus a "Recycle bin" toggle visible only to global administrators) or the recycle bin (Restore
  and Delete-permanently buttons per card, fetched from `GET /api/projects/deleted`). `renderCurrentView()`
  now awaits `renderProjects()` (previously fire-and-forget).
- Generalized the modal-close (`[data-close-modal]`) wiring to close whichever `.modal-backdrop` ancestor
  the clicked button belongs to, instead of being hardcoded to the create-issue modal only, so the new
  project modal's own close button works without duplicating logic.

### A real bug caught by this batch's own browser testing (not project-management-specific)

Testing "does a non-admin see the recycle-bin toggle" required logging out of the global-admin demo
account and into a non-admin one (`sam`) within the same page. After that switch, the app hung
indefinitely on a loading spinner instead of showing sam's dashboard. Network tracing (`page.on('request'
/'response')`) showed the login itself succeeded (`/api/auth/me` returned 200 for sam), but the very next
call was `GET /api/projects` followed by nothing -- no `/api/dashboard` call at all. The cause: `state`
(including `state.view`) is a single module-level object that was never reset on logout, so it still held
`'projects'` from the demo session; `renderCurrentView()` correctly rendered the Projects view for sam
(not a bug in that function), but the test's assumption that any fresh login lands on the dashboard was
false -- and, more importantly, this is a genuine product issue for a shared browser tab: a second user
could land on a project they don't have access to, or one already archived/deleted by the first user.
Fixed by having `showLoginScreen()` (used for both explicit logout and session-expiry) reset all of
`state` via a new shared `initialState()` function, not just clear `state.principal`.

### Verification

Standalone Playwright/Chromium scripts (same approach as prior UI batches), against a locally running
server, SQLite, demo-seeded, as the global-admin `demo` user unless noted:

1. Created project `QA`; card appears in the grid immediately.
2. Attempted a duplicate key (`qa` again); inline error shows the server's exact message ("Project key is
   already in use: QA").
3. Archived `QA`; it drops out of both the active project grid and the sidebar's project shortcuts (D87:
   archiving is not a soft-delete, but it does leave the active-projects view).
4. Created `QA2`, deleted it (moved to recycle bin), toggled to the recycle-bin view (found there),
   restored it (gone from the bin), toggled back to active (found there again), deleted it again, toggled
   to the recycle bin, permanently deleted it (gone from the bin for good).
5. Logged out and into `sam` (a non-admin): the recycle-bin toggle is absent entirely (not just
   disabled); attempting to create a project produces an inline 403 ("This action requires global
   administrator privileges") without ever creating anything -- confirming the client defers entirely to
   the server's authorization check rather than second-guessing it.
6. Re-ran the login and hierarchy/resolution-picker browser tests from the two prior batches to confirm no
   regression from the `state` reset change -- both still pass unchanged.

All checks passed after the state-reset fix; no committed test files or screenshots (scratchpad only).

### What is still not built

The issue recycle bin, bulk actions, and reorder/move still have no UI in `web/` -- see `NEXT.md`.

## 2026-07-31 — Full edit, clone, links, and watch/vote UI added to the issue drawer

Added the next slice of `web/` UI identified as missing: everything the issue drawer could reach through
the API but had no controls for -- full edit, clone, links (list/add/delete), and watch/vote.

### What changed

- `web/app.js`: `openIssue()` now fetches links/watchers/voters alongside the issue/comments (5 parallel
  requests instead of 2), and its rendering was restructured into an inner `render(editing)` closure so
  the same fetched data can re-render in read or edit mode without a refetch. Added `editFieldsMarkup()`
  (the edit-mode field set), a drawer actions row (Watch/Vote toggle buttons with live counts, Clone,
  Edit), a Links section (list with bidirectional outward/inward labels, add form, delete buttons), and a
  Parent meta row (clickable, when set).
- `web/styles.css`: `.drawer-actions`, `.link-list`/`.link-row`/`.link-form` styles.

### Two real bugs caught by this batch's own browser testing

1. **Layout bug**: `.link-list` is a CSS grid container; grid items get an implicit content-based minimum
   width unless overridden, so `.link-row` refused to shrink below its own intrinsic content width and
   visually overflowed into the drawer's meta sidebar column -- Playwright's click-action safety check
   ("element intercepts pointer events") caught this directly, refusing to click a delete-link button that
   was actually covered by the sidebar. Confirmed via `getBoundingClientRect()`: before the fix the link
   row's right edge sat at x=1090.8 while the sidebar's left edge was at x=1036 (an ~82px real overlap
   region); after adding `min-width: 0` to `.link-row`, the row's right edge moved to x=1008, clear of the
   sidebar. This was a genuine bug a real user would have hit too, not a test artifact.
2. **Test-script bug, not an app bug, but worth recording**: an earlier version of the browser test used
   an ambiguous `.link-row` selector and picked up the automatic `clones` link (created by the earlier
   Clone action, per D60) instead of the manually-added `relates_to` link, making it look like the "add
   link" feature wasn't working. Rewritten to scope the assertion to the row matching the intended link
   label, which confirmed the feature was correct all along.

### Verification

Standalone Playwright/Chromium scripts (same approach as prior UI batches), against a locally running
server, SQLite, demo-seeded:

1. Watch toggle: "☆ Watch (0)" → click → "★ Watching (1)" → click again → back to "☆ Watch (0)". Vote
   toggle: same pattern independently.
2. Clone: clicking Clone on TH-1 navigates the drawer to the new cloned issue (a fresh key), whose Links
   section already shows the automatic `clones` link back to TH-1 (D60) without any extra action.
3. Links: starting from 1 link row (the automatic clone link), adding a `relates_to` link to TH-2 brings
   the count to 2; the new row shows TH-2's actual summary and is clickable, navigating the drawer to
   TH-2; from TH-2's own drawer the same link appears (bidirectional) and its delete button works,
   bringing TH-2's link count back to 0.
4. Full edit: opening Edit on TH-1 shows editable fields pre-filled with current values; changing
   summary/description/priority/assignee/story points/labels and clicking Save persists correctly (the
   drawer re-renders in read mode showing the new values, `edit-error` stays hidden); opening Edit again,
   changing the summary, and clicking Cancel discards the change (the drawer shows the previously-saved
   summary, not the discarded one).
5. Re-ran the login and hierarchy/resolution-picker browser tests from the two prior batches against the
   same build to confirm no regression -- both still pass unchanged.

All checks passed after the layout fix; no committed test files or screenshots (scratchpad only).

### What is still not built

Project-management UI (create/archive/recycle-bin), the issue recycle bin, bulk actions, and reorder/move
still have no UI in `web/` -- see `NEXT.md`.

## 2026-07-31 — Resolution picker and Epic/parent picker added to the demo UI

Fixed two broken UI paths identified while planning the next batch of `web/` work: completing an issue
via the drawer's status `<select>` always 422'd (no `resolution` was ever sent), and the create-issue
modal had no field for `parentIssueKey` at all (and was even missing "Sub-task" from the issue-type
list), so a Sub-task could never be created from the UI and a Story/Task/Bug could never be linked to an
Epic from the UI.

### What changed

- `web/app.js`: `RESOLUTIONS` catalog and `resolutionLabel()`; `issueTypeHierarchyLevel()` (mirrors
  `Domain::issueTypeHierarchyLevel` for client-side picker narrowing only -- the server remains the actual
  source of truth); `applyStatusChange()` helper; `refreshCreateParentOptions()` which fetches the
  selected project's issues and filters them by hierarchy level to populate the Epic/parent picker.
- `web/index.html`: added `sub-task` to the create-issue-type `<select>`; added the Epic/parent `<select>`
  (`#create-parent`), hidden/shown and relabeled based on the selected issue type.
- `web/app.js` (drawer): the status `<select>`'s change handler now checks the target status's category;
  for a Done-category target it reveals an inline resolution row (select + Confirm/Cancel) instead of
  calling the API immediately; Confirm calls `applyStatusChange` with the chosen resolution. The drawer
  also now displays the issue's resolution (once set, read-only) and its parent issue key (once linked,
  clickable -- scoped with a dedicated `#drawer-parent-link` id and a single listener, deliberately
  *not* reusing the page-wide `bindIssueLinks()` helper, which would have re-registered duplicate click
  listeners on every background view element still mounted behind the drawer).
- `web/styles.css`: small additions for the resolution `<select>` and its Confirm/Cancel button row.

### A real bug caught during this batch's own browser testing

The project (`#create-project`) and issue-type (`#create-issue-type`) `<select>` elements both trigger
`refreshCreateParentOptions()` on `change`. A Playwright test that selected project then issue type in
quick succession (`page.selectOption` twice with no wait between) produced a duplicated Epic entry in the
picker -- two in-flight async calls interleaved: the first call's fetch resolved and appended options
*after* the second call had already reset and re-populated the list. Fixed with a monotonically
increasing request-id guard (`createParentRequestId`) that makes a call's `select.innerHTML +=` a no-op
once a newer call has started, so only the most recent selection's results ever get applied. The fix was
verified by re-running the same test and confirming exactly one option per matching issue.

### Verification

Standalone Playwright/Chromium scripts (same approach as the login-screen batch) against a locally running
server, SQLite, demo-seeded:

1. Selecting "Epic" as the issue type hides the Epic/parent picker entirely (an Epic cannot have a
   parent, D64); created an Epic issue successfully.
2. Selecting "Story" shows "Epic (optional)" and the picker's options include the just-created Epic
   (exactly once, confirming the race-condition fix); created a Story with that Epic as parent; the
   drawer showed a "Parent" row linking to the Epic; clicking it navigated to the Epic's own drawer.
3. Selecting "Sub-task" shows "Parent (required)"; submitting without picking one produced the server's
   exact validation message (`A sub-task must have a parent issue`) inline in the create-error banner,
   confirming the client correctly defers to server-side validation rather than duplicating it.
4. Created a fresh Task issue, then selected "Done" in the drawer's status picker: the resolution row was
   hidden before the selection and visible after; picking "Fixed" and clicking Confirm applied the status
   change with the resolution, which then displayed read-only in the drawer's Resolution row.
5. Reopening that same issue (selecting a non-Done status) cleared the resolution (server-side, D68-D70)
   and hid the resolution row again for that direction, with no picker shown (resolution is only required
   moving *into* a Done-category status, never out of one).

All checks passed after the request-id fix; no committed test files or screenshots (scratchpad only, per
the established pattern from the login-screen batch).

### What is still not built

Full edit, links, clone, watch/vote, the issue recycle bin, bulk actions, reorder/move, and
project-management UI still don't exist in `web/` -- see `NEXT.md`.

## 2026-07-31 — Minimal login screen added to the demo UI, verified with a real headless browser

Added a login screen to `web/index.html`/`app.js`/`styles.css` (the "Immediate next step" identified in
the previous entry below): the demo UI previously had no authentication flow at all, so `TicketService`'s
project-role enforcement, verified live via `curl` in the previous entry, was still unreachable from the
UI itself, and the CSRF-token attachment the UI's own `fetch` calls needed was entirely missing.

### What changed

- `web/index.html`: a `#login-screen` (email/password form) sibling to the existing `#app-shell`, both
  toggled via a shared `.hidden` class; the sidebar footer now shows the signed-in user's initials/name/
  email (previously a hardcoded "Demo User") plus a sign-out button.
- `web/app.js`: `api()` now reads the `th_csrf` cookie and attaches it as `X-CSRF-Token` on every
  non-`GET` request (previously never sent from the UI at all -- every write from the demo UI would have
  403'd the moment CSRF enforcement went live); any `401` response from any API call (except the two auth
  probes themselves) shows the login screen, handling a session expiring mid-use; `init()` now silently
  probes `GET /api/auth/me` before doing anything else and shows the login screen instead of loading data
  if that fails; the login form calls `POST /api/auth/login`, then re-probes `/api/auth/me` to populate
  the sidebar footer; the sign-out button calls `POST /api/auth/logout` and returns to the login screen.
- `web/styles.css`: a centered login card matching the existing design system (same input/button/
  form-error styles as the create-issue modal).

### Verification

Standalone Node.js scripts driving Playwright against the pre-installed Chromium
(`executablePath: '/opt/pw-browsers/chromium'`, `playwright-core` installed ad hoc into the scratchpad
directory, not committed to the repository) against a locally running `ticket-hub` server
(SQLite, auto-migrated, demo-seeded):

1. Fresh page load: `#login-screen` visible, `#app-shell` hidden, zero cookies present.
2. Submitting valid credentials (`demo@ticket-hub.local` / `demo12345`): both `th_session` (`Secure`,
   confirmed via `page.context().cookies()`) and `th_csrf` cookies get set: `#app-shell` becomes visible,
   `#login-screen` hides, and the sidebar footer shows "Demo User" -- confirming `Secure` cookies are
   **not** a blocker for local testing, since Chromium (and other major browsers) treat `localhost`/
   `127.0.0.1` as a "potentially trustworthy origin" exempt from the Secure-requires-HTTPS restriction;
   this does not weaken the production security posture (a real deployment hostname still requires TLS
   for the cookie to be set at all).
3. Every existing view renders after login: dashboard (stat cards populated), issues list (table rows),
   board (Kanban columns), projects (project cards) -- all previously untestable end-to-end because
   nothing could authenticate.
4. Created a new issue via the create-issue modal (`POST /api/issues`, a CSRF-protected write) through the
   browser's own `fetch` -- succeeded, opened the issue drawer, no error shown. This is the first time a
   CSRF-protected write from the *browser UI itself* (not `curl` with a manually-added header) was
   confirmed to work, proving `app.js`'s new automatic CSRF-header attachment functions correctly.
5. Changed the new issue's status via the drawer's status `<select>` (`PATCH .../status`, also
   CSRF-protected) -- succeeded, confirmation toast shown.
6. Clicked sign-out: both cookies cleared, `#login-screen` shown again. Reloaded the page afterward: still
   on the login screen (not silently re-entering the app), confirming the server actually invalidated the
   session rather than the UI merely hiding it client-side.
7. Submitted a wrong password: inline error message shown ("Invalid email or password"), `#app-shell`
   never becomes visible.

All seven checks passed on the first attempt; no code changes were needed after the initial
implementation. Screenshots were captured for visual sanity-check (login screen and post-login dashboard)
and discarded after review (not committed -- generated artifacts, not source).

### What is still not built

Only the login screen exists. There is still no UI for project management (create/archive/recycle-bin),
issue hierarchy/resolution pickers, full edit, links, clone, watch/vote, the issue recycle bin, bulk
actions, or the reorder/move actions added earlier this session -- all of those remain reachable only via
direct API calls (all live-verified via `curl`, see the entry below), not through the demo UI. See
`NEXT.md` for the itemized list.

## 2026-07-31 — Server target verified end-to-end for the first time (Crow build succeeded)

Every prior entry in this log recorded the same standing limitation: outbound access to `github.com` was
blocked, so the Crow-based `ticket-hub` server target had never been compiled, and every Phase 1-3 route
in `src/web/Api.cpp` was written blind against established patterns. That changed this session: network
access to `github.com` is now reachable from the sandbox, so the server target was built and exercised
against a live HTTP server for the first time.

### What changed in the environment

- `git ls-remote https://github.com/CrowCpp/Crow.git` succeeds; CMake's `FetchContent_Declare`/
  `FetchContent_MakeAvailable(Crow)` fetches Crow 1.3.3 successfully.
- Crow 1.3.3's `CMakeLists.txt` additionally requires standalone `asio` (not Boost.Asio), which was not
  preinstalled. Installed via `sudo apt-get install -y libasio-dev` (already listed as a dependency in
  `README.md`'s "Requirements"/apt install line, so no doc change was needed there) — this pulled in
  `libboost-dev` and related packages as transitive dependencies of the Ubuntu package, not because
  Ticket Hub itself needs Boost.

### Build

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel "$(nproc)"
```

Configured and built clean on the first attempt after `libasio-dev` was installed. `-Wall -Wextra
-Wpedantic -Wconversion -Wshadow` surfaces a large number of warnings **from Crow's own headers**
(`mustache.h`, `routing.h`, `http_server.h`, `http_connection.h`, `parser.h` — mostly `-Wconversion` on
`size_t`/`uint64_t` narrowing in Crow's implementation) — expected third-party noise, not addressable in
this repository. Isolating just `src/main.cpp`, `src/web/Api.cpp`, and `src/web/HttpServer.cpp` (the only
files this repository owns that only compile when `TICKETHUB_BUILD_SERVER=ON`) confirms **zero warnings
and zero errors from Ticket Hub's own code** — every route added across Phases 1-3, blind against
established patterns for months of prior batches, compiled correctly the first time it was actually
type-checked. `ctest --output-on-failure` is still 7/7 with the server target also built.

### Live HTTP smoke test

Launched the built `ticket-hub` binary against a fresh SQLite database (`TICKETHUB_AUTO_MIGRATE=true
TICKETHUB_SEED_DEMO=true`, bound to `127.0.0.1:18080`) and drove it with `curl`, covering essentially
every route in the "Prototype API" table in `README.md`:

- `GET /api/health` — 200, correct backend/version.
- `POST /api/auth/login` (demo, alex) — 200, `Set-Cookie` for both `th_session` (HttpOnly) and `th_csrf`
  (readable) as designed; `GET /api/auth/me` reflects the session; `POST /api/auth/logout` then
  `GET /api/auth/me` — 401, confirming the session is actually gone, not just cookie-cleared client-side.
- CSRF enforcement: `POST /api/issues` without `X-CSRF-Token` — 403; with a valid token — 201.
- Project-role enforcement: Sam (TH member, not a WEB member) creating a WEB issue — 403; Alex (not a
  global admin) creating a project — 403; the global admin — 201.
- Fixed workflow rules: completing a fresh issue without `resolution` — 422; with a valid `resolution` —
  200, `resolution` and incremented `version` present in the response; a stale `expectedVersion` — 409.
  (An earlier pass in this same sweep initially targeted `TH-1`, which the demo seed already has in
  `done` status — that call correctly returned 200/no-op rather than 422, which is the documented
  "resolution is ignored when the status does not actually change" behavior, not a bug; re-ran against a
  freshly created issue to actually exercise the required-resolution path.)
- **`POST /api/issues/{key}/reorder`** (D31, this session's most recent batch): reordering an issue
  before another renumbers both correctly (`rankOrder` reflects the new order on `GET`).
- **`POST /api/issues/{key}/move`** (D37, this session's most recent batch): moving `TH-7` into `WEB`
  returns the issue with `projectKey: "WEB"`, `key: "WEB-3"`; `GET /api/issues/TH-7` (the vacated key)
  still resolves to the same issue via the `issue_key_aliases` alias written by `moveIssue` — confirming
  the alias mechanism works through the real HTTP/JSON layer, not just the database layer directly.
- Full-replacement issue edit (`PATCH /api/issues/{key}`), cloning (`POST .../clone`), issue links
  (`POST`/`GET .../links`, `DELETE /api/issue-links/{id}`), watching/voting and their unwatch/unvote/list
  counterparts, the issue recycle bin (`DELETE`/`POST .../restore`/`DELETE .../permanent`,
  `GET /api/issues/deleted`), all four bulk actions (`POST /api/issues/bulk/{status,assign,label,delete}`
  — confirmed `succeeded`/`failed` key lists), comments (`POST`/`GET .../comments`), the full project
  lifecycle (`POST /api/projects`, `PATCH .../archived`, `DELETE`/`POST .../restore`/
  `DELETE .../permanent`, `GET /api/projects/deleted`), and the anonymous-read-access toggle
  (`GET`/`PUT /api/settings/anonymous-read`, confirmed `GET /api/projects` goes 401 → 200 as the toggle
  flips, and that only a global admin may flip it) — every one exercised live, every one behaved exactly
  as documented in `README.md`'s "Prototype API" section. **Zero bugs found** in `Api.cpp` across this
  entire sweep.
- The scratch SQLite database, server process, and all temporary cookie-jar files were removed after the
  sweep; nothing from this verification was committed.

### What is still not verified

There is still no login page or any authentication UI in `web/` — the demo UI browses/creates issues
without ever calling `/api/auth/login`, so it will start failing 401s against a real deployment where
session enforcement is live end-to-end, exactly as `README.md` already notes. Building a minimal login
page (and ideally project-management/hierarchy/reorder/move UI) is the next real gap, not a verification
gap — see `NEXT.md`.

## 2026-07-31 — Phase 3 complete at the core/CLI/test layer (manual ordering and moving between projects, reduced scope)

Verified in the same session/environment as the batches below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (restarted first, as in every prior batch this
session; freshly created scratch database/role, dropped afterward).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel "$(nproc)"
ctest --test-dir build --output-on-failure
```

New `IDatabase::reorderIssue`/`moveIssue` methods in both adapters, plus `TicketService::reorderIssue`/
`moveIssue`, compiled cleanly. Also verified the SQLite-only (`-DTICKETHUB_WITH_POSTGRES=OFF`) and
PostgreSQL-only (`-DTICKETHUB_WITH_SQLITE=OFF`) configurations build clean, both with
`-DTICKETHUB_BUILD_SERVER=OFF`.

A migration bug was caught during this batch and fixed before it reached a committed test failure: the
`007_ranking.sql` backfill (`UPDATE issues SET rank_order = issue_number`) only reaches rows that exist at
migration-apply time, but `002_seed_demo.sql` is applied through a separate, non-checksummed code path
(`discoverMigrationFiles` excludes `_seed_` filenames from the ordered migration run) that in practice
runs *after* `migrate()` completes -- so every seeded issue was getting the column's `DEFAULT 0` instead
of a real rank. Fixed by setting `rank_order` explicitly in the seed `INSERT` (both backends) rather than
relying on the migration backfill for seed data.

### Passing tests (7/7 — same binaries, extended coverage)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-identity-tests`,
   `ticket-hub-workflow-tests`, `ticket-hub-crypto-tests` — unchanged, all still passing.
2. `ticket-hub-sqlite-integration-tests` — extended: reordering TH-3 before TH-1 renumbers the whole
   project so TH-3 ranks lower than TH-1; reordering TH-3 with no anchor appends it to the end (ranks
   higher than every other TH issue); reordering relative to an issue in a different project and
   reordering an issue before itself are both rejected; moving TH-2 into WEB allocates `WEB-3`, appends
   its rank after WEB's existing issues, and the vacated `TH-2` key resolves via `issue_key_aliases` back
   to `WEB-3`, with one `issue_history` row (`field_name = 'project'`); moving to the issue's own project,
   to an unknown project, an issue that has children, and an issue that has a parent are all rejected.
3. `ticket-hub-authorization-tests` — extended: a non-member cannot reorder another project's issue; a TH
   member can reorder a TH issue; moving an issue requires project-Member-or-above on **both** the source
   and target projects -- a TH member who is not a WEB member is rejected moving either direction, while a
   member of both (TH member, WEB admin) succeeds.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: ran a standalone program (`pg_rank_move_smoke.cpp`, not committed)
  against `PostgresDatabase` directly, covering the same renumbering, cross-project-anchor-rejection,
  move/alias/history, same-project-rejection, and parent/children-rejection cases as the SQLite test. All
  assertions passed. The scratch database and role were dropped after verification.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all seven test binaries
  compile and link cleanly, including in the SQLite-only and PostgreSQL-only build configurations.

### Environment limitations (unchanged)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com` remains blocked. The new
`POST /api/issues/{key}/reorder` and `POST /api/issues/{key}/move` routes, and the new `rankOrder` field
on the issue JSON representation, follow the same patterns as the already-unverified Phase 1-3 routes and
carry the same caveat.

## 2026-07-31 — Phase 3, partial continued (issue recycle bin and bulk actions, reduced scope)

Verified in the same session/environment as the batches below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (the server needed restarting first, as in every
prior batch this session; freshly created scratch database/role, dropped afterward).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings, including through
the four new `IDatabase` methods (`softDeleteIssue`/`restoreIssue`/`listDeletedIssues`/
`permanentlyDeleteIssue`) forcing a rebuild through both adapters and `TicketService`. Bulk actions added
no new `IDatabase` methods at all -- they compose the existing single-issue operations.

### Passing tests (7/7 — same binaries, extended coverage)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-identity-tests`,
   `ticket-hub-workflow-tests`, `ticket-hub-crypto-tests` — unchanged, all still passing.
2. `ticket-hub-sqlite-integration-tests` — extended: soft-deletes the previously-created issue, asserts
   it disappears from ordinary lookup, asserts soft-deleting it again is a no-op, asserts it appears in
   `listDeletedIssues`, asserts it can be restored and found again, re-deletes it, permanently deletes
   it, asserts a repeat permanent delete returns `false`, and asserts its comments are gone from the
   `comments` table afterward (direct `COUNT(*)` query, confirming the `ON DELETE CASCADE` foreign key
   actually fires, not just that the issue itself can no longer be looked up).
3. `ticket-hub-authorization-tests` — extended: a non-member cannot soft-delete an issue; a project admin
   (not global admin) can; listing/restoring/permanently-deleting are rejected for the project admin and
   permitted for the global administrator, mirroring the project recycle bin's D88 split exactly. Bulk
   actions: `bulkAssign` on a mixed batch of two accessible TH issues, one inaccessible WEB issue, and one
   unknown key reports 2 succeeded / 2 failed; `bulkAddLabel` and `bulkChangeStatus` succeed for an
   accessible batch; `bulkDelete` soft-deletes a batch and the deleted issues are confirmed gone from
   ordinary lookup afterward.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: ran `ticket-hub-cli migrate`/`seed-demo`, then compiled and ran a
  standalone program (`pg_recycle_bulk_smoke.cpp`, not committed) exercising `PostgresDatabase::
  softDeleteIssue`/`restoreIssue`/`listDeletedIssues`/`permanentlyDeleteIssue` directly, then wrapping
  the same `PostgresDatabase` in a `TicketService` to exercise `bulkAssign` (including the unknown-key
  partial-failure case), `bulkAddLabel`, `bulkChangeStatus`, and `bulkDelete`. All 15 assertions passed.
  The scratch database and role were dropped after verification.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all seven test binaries
  compile and link cleanly, including in the SQLite-only and PostgreSQL-only build configurations.

### Environment limitations (unchanged)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com` remains blocked. The new
`DELETE /api/issues/{key}`, `GET /api/issues/deleted`, `POST /api/issues/{key}/restore`,
`DELETE /api/issues/{key}/permanent`, and `POST /api/issues/bulk/{status,assign,label,delete}` routes
follow the same patterns as the already-unverified Phase 1-3 routes and carry the same caveat.

## 2026-07-31 — Phase 3, partial continued (watchers and voting, reduced scope)

Verified in the same session/environment as the batches below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (the server needed restarting first, as in every
prior batch this session; freshly created scratch database/role, dropped afterward).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings, including through
the six new `IDatabase` methods (`watchIssue`/`unwatchIssue`/`listWatchers`/`voteIssue`/`unvoteIssue`/
`listVoters`) forcing a rebuild through both adapters and `TicketService`.

### Passing tests (7/7 — same binaries, extended coverage)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-identity-tests`,
   `ticket-hub-workflow-tests`, `ticket-hub-crypto-tests` — unchanged, all still passing.
2. `ticket-hub-sqlite-integration-tests` — extended: watches the previously-created issue with two
   different users, asserts watching again is a no-op, asserts both watchers are listed, asserts
   unwatching removes one and unwatching again is a no-op; the same sequence for voting; asserts
   watching an unknown issue key raises `std::invalid_argument`.
3. `ticket-hub-authorization-tests` — extended: Sam, who is not a member of the WEB project at all
   (unlike every other actor/action tested in this file), can still watch and vote on a WEB issue,
   confirming watch/vote are deliberately exempt from the project-role check every other issue write
   enforces; watching/voting again is a no-op; both are visible to any authenticated reader.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: ran `ticket-hub-cli migrate`/`seed-demo` (confirmed `issue_watchers`
  and `issue_votes` were created with the expected columns, composite primary key, and `ON DELETE
  CASCADE` foreign keys via `psql \d`), then compiled and ran a standalone program
  (`pg_watch_vote_smoke.cpp`, not committed) exercising `PostgresDatabase::watchIssue`/`unwatchIssue`/
  `listWatchers`/`voteIssue`/`unvoteIssue`/`listVoters` directly: idempotency of watch/vote, two
  independent watchers, listing, unwatch/unvote removing exactly the right row, and voting on an unknown
  issue key raising `std::invalid_argument`. All 13 assertions passed. The scratch database and role
  were dropped after verification.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all seven test binaries
  compile and link cleanly, including in the SQLite-only and PostgreSQL-only build configurations.

### Environment limitations (unchanged)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com` remains blocked. The new
`POST`/`DELETE /api/issues/{key}/watch`, `GET /api/issues/{key}/watchers`,
`POST`/`DELETE /api/issues/{key}/vote`, and `GET /api/issues/{key}/voters` routes follow the same
patterns as the already-unverified Phase 1-3 routes and carry the same caveat.

## 2026-07-31 — Phase 3, partial continued (issue links and cloning, reduced scope)

Verified in the same session/environment as the batches below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (freshly created for this batch and dropped
afterward, same as every prior PostgreSQL verification this session).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings, including through
the four new `IDatabase` methods (`createIssueLink`/`listIssueLinks`/`findIssueLinkById`/
`deleteIssueLink`) forcing a rebuild through both adapters and `TicketService`.

### Passing tests (7/7 — same binaries, extended coverage)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-identity-tests`,
   `ticket-hub-crypto-tests` — unchanged, all still passing.
2. `ticket-hub-sqlite-integration-tests` — extended: creates a link from the previously-created issue to
   a seeded one, asserts it is visible as outward from the source and inward from the target, asserts an
   exact-duplicate link and a self-link are both rejected (the self-link by the database `CHECK`
   constraint), asserts `findIssueLinkById` resolves both ends, and asserts the link can be deleted
   (and that deleting it again returns `false`).
3. `ticket-hub-workflow-tests` — extended: clones an issue and asserts summary/description/type/
   priority/labels are copied but assignee and the parent/Epic link are not; asserts the clone has an
   outward `clones` link to the original and the original has the inverse inward link; clones a Sub-task
   and asserts its original parent is retained (the structural exception); and a basic link
   create/list/delete lifecycle through `TicketService`, including rejecting a self-link and an unknown
   link type.
4. `ticket-hub-authorization-tests` — extended: a non-member cannot clone another project's issue; a
   project admin can; creating a link between a project the actor can access and one they cannot is
   rejected even though the source project is fine; a non-member of either linked project cannot delete
   the link; a member of both can.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: created a scratch `tickethub` database/role, ran `ticket-hub-cli
  migrate`/`seed-demo`, then compiled and ran a standalone program (`pg_links_smoke.cpp`, not committed)
  exercising both the raw `PostgresDatabase` link methods (create/list-from-both-ends/duplicate-and-
  self-link rejection/find-by-id/delete) and, through a `TicketService` wrapping the same
  `PostgresDatabase`, `cloneIssue` (field-copy correctness, the automatic `clones` link, and the
  sub-task-parent-retention special case). All 13 assertions passed. The scratch database and role were
  dropped after verification.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all seven test binaries
  compile and link cleanly, including in the SQLite-only and PostgreSQL-only build configurations.

### Environment limitations (unchanged)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com` remains blocked. The new
`POST /api/issues/{key}/clone`, `GET`/`POST /api/issues/{key}/links`, and
`DELETE /api/issue-links/{id}` routes follow the same patterns as the already-unverified Phase 1-3
routes and carry the same caveat.

## 2026-07-31 — Phase 3, partial continued (full issue edit, reduced scope)

Verified in the same session/environment as the batches below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (freshly created for this batch and dropped
afterward, same as every prior PostgreSQL verification this session).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings through the
`IDatabase::editIssue` addition (interface change forced a rebuild through `TicketService` and both
adapters, same discipline as the `changeIssueStatus` signature change in the prior batch).

### Passing tests (7/7 — same binaries, extended coverage)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-identity-tests`,
   `ticket-hub-workflow-tests`, `ticket-hub-crypto-tests` — unchanged, all still passing.
2. `ticket-hub-sqlite-integration-tests` — extended: edits every standard field on the previously-created
   issue (summary, description, priority, assignee, story points, due date, labels), asserts the
   optimistic-lock version increments and the resolution set by the earlier status-change test is left
   undisturbed, asserts a stale-version edit raises `Domain::ConcurrencyConflict`, asserts the assignee
   can be cleared, and asserts at least six `issue_history` rows were written (one per changed field).
3. `ticket-hub-authorization-tests` — extended: a non-member cannot edit another project's issue
   (`Domain::Forbidden`); a project member can edit a project issue; editing an unknown issue key returns
   `std::nullopt` rather than throwing.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: created a scratch `tickethub` database/role (the server needed
  restarting first, as in the prior batch), ran `ticket-hub-cli migrate`/`seed-demo`, then compiled and
  ran a standalone program (`pg_edit_smoke.cpp`, not committed) linking directly against
  `PostgresDatabase`: created an issue, edited every field (summary, description, priority, assignee,
  story points, due date, labels) and confirmed each was persisted and the version incremented, confirmed
  a stale-version edit raises `ConcurrencyConflict`, confirmed the assignee can be cleared, and confirmed
  editing an unknown issue key returns `std::nullopt`. All 11 assertions passed. The scratch database and
  role were dropped after verification.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all seven test binaries
  compile and link cleanly, including in the SQLite-only and PostgreSQL-only build configurations.

### Environment limitations (unchanged)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com` remains blocked. The new
`PATCH /api/issues/{key}` full-edit route follows the same patterns as the already-unverified Phase 1-3
routes and carries the same caveat. While adding it, three existing routes (`POST /api/issues`,
`PATCH /api/issues/{key}/status`, `POST /api/issues/{key}/comments`) were found missing a
`catch (const Domain::Forbidden&)` handler and fixed -- this was caught by code inspection while editing
the surrounding code, not by compiling, since the file still cannot be built in this sandbox.

## 2026-07-31 — Phase 3, partial (fixed workflow and hierarchy, reduced scope)

Verified in the same session/environment as Phases 1-2 below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (freshly created for this batch's verification and
dropped afterward, as in Phase 2).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings, including after
the `IDatabase::changeIssueStatus` signature change forced updates through `TicketService` and both
adapters, and after the `Domain::Issue`/`CreateIssueRequest` field additions.

### Passing tests (7/7)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-identity-tests`,
   `ticket-hub-authorization-tests`, `ticket-hub-crypto-tests` — unchanged from Phase 1/2, all still
   passing.
2. `ticket-hub-sqlite-integration-tests` — updated: the existing `changeIssueStatus` call to transition
   an issue to `done` now passes a `resolution` (the call would otherwise fail Phase 3's new
   requirement), and asserts the resolution is stored; the deliberately-stale-version call that exercises
   `ConcurrencyConflict` was updated for the new parameter position but is otherwise unchanged.
3. `ticket-hub-workflow-tests` (new) — SQLite, exercised through `TicketService`:
   - hierarchy: a sub-task without a parent is rejected; an epic cannot have a parent; a story's parent
     must be an epic, not another story; a story can link to an epic in the same project; a sub-task's
     parent must be a story/task/bug, not an epic; a parent issue must be in the same project;
   - workflow: transitioning to Done without a resolution is rejected (`Domain::WorkflowViolation`); an
     unknown resolution key is rejected; a valid resolution is stored on completion; reopening does not
     require a resolution and clears the stored one (D70); a parent cannot complete while a sub-task is
     unfinished (D68) and can once the sub-task is done; reopening a completed parent leaves its
     sub-task's status untouched (D69).

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: created a scratch `tickethub` database/role (the server itself needed
  restarting first -- it was not still running from the Phase 2 session), ran `ticket-hub-cli
  migrate`/`seed-demo`, then compiled and ran a standalone program (`pg_phase3_smoke.cpp`, not committed)
  linking directly against `PostgresDatabase`: created an Epic and a Story linked to it (verifying
  `parent_issue_id` round-trips through `createIssue`/`findIssueByKey`), confirmed the Done-without-
  resolution rejection, confirmed a valid resolution is stored and then cleared on reopen, created a
  Sub-task under the Story and confirmed the parent cannot complete while it's unfinished, confirmed
  completing the Sub-task then lets the parent complete, and confirmed reopening the parent afterward
  leaves the Sub-task's status untouched. All 12 assertions passed. The scratch database and role were
  dropped after verification.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all seven test binaries
  compile and link cleanly, including in the SQLite-only and PostgreSQL-only build configurations
  (`-DTICKETHUB_WITH_POSTGRES=OFF` / `-DTICKETHUB_WITH_SQLITE=OFF`).

### Environment limitations (unchanged from Phase 1-2)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com`, needed for CMake `FetchContent` to fetch Crow,
remains blocked. The Phase 3 route changes (`parentIssueKey` on `POST /api/issues`, `resolution` on
`PATCH /api/issues/{key}/status`, `Domain::WorkflowViolation` mapped to HTTP 422, `resolution` added to
issue JSON responses) follow the same patterns as the already-unverified Phase 1/2 routes and carry the
same caveat.

## 2026-07-31 — Phase 2 (authorization and projects, reduced scope)

Verified in the same session/environment as Phase 1 below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (freshly created for this phase's verification and
dropped afterward).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings, in both the
initial database-layer compile and every subsequent incremental compile as the application layer
(`TicketService`), the new test binary, and the read-access-gating changes were added.

### Passing tests (6/6)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-sqlite-integration-tests`,
   `ticket-hub-identity-tests`, `ticket-hub-crypto-tests` — unchanged from Phase 1, all still passing.
2. `ticket-hub-authorization-tests` (new) — SQLite, exercised through `TicketService` (not the raw
   database adapter, unlike the other integration tests) so the authorization layer itself is under
   test:
   - a project non-member cannot create an issue, change status, or comment in another project
     (`Domain::Forbidden`); a project member can; a project admin (not global admin) can too;
   - not-found semantics are unaffected by authorization: changing the status of an unknown issue still
     returns `false` (not `Forbidden`), commenting on one still raises `std::invalid_argument`;
   - the anonymous-read-access toggle: off by default, an anonymous caller is rejected
     (`Domain::AuthenticationRequired`) while off and admitted once a global administrator turns it on,
     an authenticated caller can always read regardless of the toggle, and only a global administrator
     may flip it;
   - the full project lifecycle authorization matrix: creation is global-admin-only even for another
     project's admin; archiving requires at least project-admin; the recycle bin
     (`listDeletedProjects`/`restoreProject`/`permanentlyDeleteProject`) is global-administrator-only even
     for the project's own admin, matching D88 ("admin restore or permanent delete"); soft-delete
     (moving to the bin) is available to a project admin.
   - One real bug caught by this suite before it was trusted: the test's first assertion expected an
     archived project to still appear (with `archived: true`) in `listProjects()`. The actual (pre-existing,
     correct) behavior is that `listProjects()` is the *active* project list and excludes archived/deleted
     projects entirely, per D87 ("leaves active lists"). The test assertion was wrong, not the
     implementation; it was corrected to assert the project's absence, then its return once unarchived.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: created a scratch `tickethub` database/role, ran `ticket-hub-cli
  migrate`/`seed-demo`/`create-user` against it, then compiled and ran a standalone program
  (`pg_phase2_smoke.cpp`, not committed) linking directly against `PostgresDatabase` to exercise every
  Phase 2 method that could not be covered by the SQLite-only `authorization_integration_tests`:
  `getSetting`/`setSetting` (including the `ON CONFLICT` upsert), `findProjectRoleByKey`, `createProject`
  (including the auto-inserted creator-as-admin membership row), `setProjectArchived`,
  `softDeleteProject` (including its idempotent-false second call), `listDeletedProjects` (the `LATERAL`
  join and the on-demand `INTERVAL '90 days'` purge), `restoreProject`, and `permanentlyDeleteProject`
  (including its idempotent-false second call). All 17 assertions passed. The scratch database and role
  were dropped after verification; nothing PostgreSQL-specific was left running.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all six test binaries
  compile and link cleanly.

### Environment limitations (unchanged from Phase 1)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com`, needed for CMake `FetchContent` to fetch Crow,
remains blocked. The new Phase 2 routes (`POST/PATCH/DELETE /api/projects/...`,
`GET/PUT /api/settings/anonymous-read`) and the read-route changes to resolve an optional `Principal`
follow the same patterns as the already-unverified Phase 1 routes and carry the same caveat: build and
smoke-test against a real Crow checkout before trusting them.

## 2026-07-31 — Phase 1 (identity and sessions, reduced scope)

Verified in the session environment with GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1, libpq 16.14, and a
live local PostgreSQL 16.14 server.

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings.

### Passing tests (5/5)

1. `ticket-hub-domain-tests` — project/issue key and label validation (unchanged), plus new email
   normalization/validation and password/create-user validation coverage.
2. `ticket-hub-migration-tests` — unchanged (ordered discovery, seed exclusion, stable checksums).
3. `ticket-hub-sqlite-integration-tests` — unchanged issue/comment/status coverage, updated to the
   email/user-id identity model (`assigneeEmail`, real user ids instead of usernames for
   reporter/actor/author).
4. `ticket-hub-identity-tests` (new) — SQLite: seeded-user login (validates the seed's own Argon2id
   hash), session validate/logout, unknown-token/empty-token rejection, identical error for
   wrong-password vs. unknown-email (anti-enumeration check), minimal login-attempt lockout tripping
   and staying locked even against a subsequently-correct password, administrator account creation,
   duplicate-email rejection, invalid-request rejection.
5. `ticket-hub-crypto-tests` (new) — SHA-256 known-answer vectors (empty string, `"abc"`, and the
   55/56/64-byte padding-boundary cases), Argon2id encode/verify round-trip, random-token uniqueness.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server** (installed locally in this session, not just SQLite): ran
  `ticket-hub-cli migrate`, `seed-demo`, and `create-user` against a real database, inspected the
  resulting `users`/`local_credentials` schema and rows with `psql`, and ran a standalone program
  exercising `AuthService::login` → `validateSession` → `logout` end-to-end against it. This is strictly
  more PostgreSQL coverage than any prior session recorded in this repository, which previously noted
  "PostgreSQL runtime integration tests require an external test server."
- Confirmed the SQLite migration-runner's foreign-key-safe table-rebuild pattern (`PRAGMA foreign_keys
  OFF` → rebuild → `PRAGMA foreign_keys ON` + `PRAGMA foreign_key_check`) against a minimal standalone
  reproduction before trusting it in `004_identity.sql`, after the first attempt (a plain `ALTER TABLE
  ... DROP COLUMN username`) failed with SQLite's documented "cannot drop UNIQUE column" restriction.
- Both SQLite and PostgreSQL adapters, and the `ticket-hub-cli` target, compile and link cleanly.
- `cmake/FindArgon2.cmake` (new, no upstream CMake package exists for libargon2) resolves correctly via
  `find_package(Argon2 REQUIRED)`.

### Environment limitations

- **The `ticket-hub` server target (Crow) was not compiled.** Outbound HTTPS to `github.com` — needed
  for CMake `FetchContent` to fetch Crow — was blocked by this session's network egress policy (`403`).
  This is the same limitation recorded in every prior session
  (`handoff/IMPLEMENTATION_STATE.md`, the entry below). `src/web/Api.cpp`, `src/web/HttpServer.cpp`, and
  `src/main.cpp` were updated to match the new `AuthService`/`Principal`-based signatures and to add
  `/api/auth/login`, `/api/auth/logout`, `/api/auth/me`, and session-cookie/CSRF protection on existing
  write routes, but **none of this has been compiled or run**. Build and smoke-test the server target
  against a real Crow checkout before trusting it.
- No login page exists in `web/` yet; the demo UI does not call `/api/auth/login`.
- The 15-minute login lockout window could not be tested past the "still locked immediately after
  tripping" case — advancing wall-clock time inside a test is out of scope for this pass.

---

## 2026-07-30 — 0.2.0 baseline (prior session)

Verified in the provided Linux environment with GCC 14.2.0, SQLite 3.46.1 and libpq 17.9.

### Core configuration

```bash
cmake -S . -B build-core \
  -DTICKETHUB_BUILD_SERVER=OFF \
  -DTICKETHUB_WITH_POSTGRES=ON \
  -DTICKETHUB_WITH_SQLITE=ON
cmake --build build-core --parallel 4
ctest --test-dir build-core --output-on-failure
```

### Passing tests

1. `ticket-hub-domain-tests`
   - project key normalization and 2–12-character validation,
   - issue-key normalization and validation,
   - label normalization,
   - issue-create validation.

2. `ticket-hub-migration-tests`
   - ordered discovery,
   - exclusion of demo seed files,
   - stable content checksums.

3. `ticket-hub-sqlite-integration-tests`
   - ordered migration application and idempotence,
   - idempotent demo seed,
   - two projects returned exactly once,
   - eight seeded issues,
   - transactional creation of `TH-7`,
   - labels and detail readback,
   - permanent old-key alias resolution,
   - optimistic status update and version increment,
   - rejection of a stale update,
   - comment creation/listing,
   - dashboard counts and ordering,
   - rejection of a historical migration modified after application.

### Additional static validation

- Both SQLite and PostgreSQL adapters compile and link into `ticket-hub-core`.
- `ticket-hub-cli` compiles, reports version 0.2.0 and applies the schema to a fresh SQLite database.
- SQLite-only and PostgreSQL-only configurations both compile; all tests available in each configuration pass.
- Frontend JavaScript is checked with Node syntax validation when Node is available.

### Environment limitations

- A PostgreSQL server was not available for a live adapter integration test.
- Full Crow server configuration was attempted but could not fetch Crow because the sandbox could not resolve GitHub and no installed Crow package was available. The HTTP-layer source changes therefore were not compiled in this environment.
