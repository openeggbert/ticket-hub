# Ticket Hub

Ticket Hub is a self-hosted Jira-like Software issue tracker written in C++20. It uses Crow for HTTP, vanilla HTML/CSS/JavaScript for the web interface, PostgreSQL as the primary production database and SQLite as a smaller single-process backend.

License: MIT.  
Main namespace: `TicketHub`.

## Project status

The current build target is the **reduced-scope V1** (see `REDUCED_SCOPE_SPECIFICATION.md`), not the
original full Jira-like plan in `SPECIFICATION.md`, which remains only as a long-term aspirational
reference. **The entire reduced-scope V1 roadmap is now complete** (`docs/REDUCED_SCOPE_ROADMAP.md`,
Milestones 1-4 / Phases 1-8), including Docker/Compose packaging, light/dark theme, an accessibility
baseline pass, and a threat-model/security self-review (`docs/THREAT_MODEL.md`) that found and fixed a
real access-control bug. Four batches of optional, non-roadmap follow-up have been added since -- a web UI
for managing personal access tokens/active sessions, re-typing/re-parenting an issue after creation (the
one gap left open since Phase 3), Kanban board drag-and-drop, and a bulk Done-status picker plus keyboard
multi-select -- which closes out the entire optional-follow-up list identified when the roadmap closed.
See `NEXT.md`'s "The roadmap is now complete" section for the exact closing detail and `docs/VERIFICATION.md`
for exactly what was tested and how.

Implemented now:

- projects and transactional project-local issue keys,
- issue list/detail/create, status changes, labels and comments,
- **a demo web UI covering every Phase 1-3 write route** (login, hierarchy/resolution pickers, full issue
  edit/clone/links/watch-vote/delete, project management, both recycle bins, reorder/move/bulk actions —
  browser-verified with Playwright/Chromium, see "Server verification" below) plus a simple Kanban board,
- **local accounts: Argon2id password hashing, server-side sessions, minimal login-attempt lockout**
  (`AuthService` — Phase 1 of `docs/REDUCED_SCOPE_ROADMAP.md`),
- **administrator-only account creation via `ticket-hub-cli create-user`** — there is no
  self-registration or invitation flow in V1,
- **fixed project roles (Viewer/Member/Admin) and a global administrator flag, enforced on every issue
  and project write** (`TicketService::requireProjectRole`/`requireGlobalAdmin` — Phase 2 of
  `docs/REDUCED_SCOPE_ROADMAP.md`),
- **project lifecycle: create (global admin), archive/unarchive (project admin), recycle bin with fixed
  90-day on-demand retention, restore, and permanent delete (global admin)**,
- **installation-wide anonymous read-access toggle, off by default** — every read use case takes an
  optional `Principal`; an anonymous caller is rejected unless the toggle is on,
- **fixed Epic → Story/Task/Bug → Sub-task hierarchy, enforced on issue creation** (`TicketService::
  requireValidHierarchy` — Phase 3 of `docs/REDUCED_SCOPE_ROADMAP.md`, partial): a Sub-task requires a
  same-project Story/Task/Bug parent, an Epic may not have a parent, an optional Story/Task/Bug parent
  must be an Epic,
- **the fixed workflow's hardcoded transition rules, enforced transactionally in `changeIssueStatus`**:
  a resolution is required to complete an issue and is cleared automatically on reopen, and an issue
  cannot complete while it has an unfinished sub-task,
- **full-replacement issue edit with optimistic locking** (D129): summary, description, priority,
  assignee, story points, due date and labels, sharing the same `expectedVersion`/409 contract as status
  changes, with one `issue_history` row per field that actually changed,
- **the fixed issue-link catalog** (D17): `blocks`/`relates_to`/`duplicates`/`clones`, each visible from
  both linked issues with the correct outward/inward label; creating or deleting a link requires access
  to both projects,
- **simple field-copy cloning** (D60): summary/description/type/priority/labels copied into a new issue,
  with an automatic `clones` link back to the original,
- **self-service watching and voting** (D20/D79): any authenticated user may watch or vote on any issue
  — the one write with no project-role requirement — idempotent on repeat, with a visible watcher/voter
  list,
- **issue recycle bin** (D22): soft delete (project admin), restore/list/permanent delete (global admin
  only), fixed 90-day on-demand retention — mirrors the project recycle bin exactly,
- **simple bulk actions** (D36): status/assignee/label/recycle applied to a list of issue keys, each
  through the same single-issue operation and authorization as doing it one at a time; a partial failure
  is reported, not rolled back,
- **simple integer manual ordering with renumbering** (D31): a per-project `rank_order`, replacing the
  never-used LexoRank-style `rank_value` placeholder; moving an issue renumbers the whole project's issue
  list in one pass rather than using a minimal-diff/fractional scheme,
- **moving an issue to a different project** (D37): no compatibility check is needed since every project
  shares the same fixed types/workflow/fields — a move is a `project_id` change plus a freshly allocated
  key/number, exactly like creating a new issue there; rejected if the issue has a parent or any children;
  requires project-Member-or-above on both the source and target projects,
- **comment editing and tombstone delete** (D81/D82/D83, Phase 4, partial): an `edited_at` timestamp
  instead of a version-history table; soft-delete via the same columns issues/projects already use, no
  separate admin recycle-bin API for comments; simplified permissions — the comment's own author can
  always edit/delete it, otherwise the actor needs project-Admin-or-above (or global admin),
- every issue/comment/project write now takes an explicit `Principal` instead of a fixed demo user,
- PostgreSQL and SQLite adapters,
- ordered schema migration discovery with stored checksums,
- PostgreSQL migration advisory lock,
- issue optimistic-lock versioning for status updates,
- permanent project key-alias schema foundation, and a permanent issue key-alias mechanism actually
  written to by `moveIssue` (D38): the vacated key stays permanently resolvable to the moved issue,
- recycle-bin schema foundations and live-query filtering,
- domain, migration, crypto, SQLite integration, identity, authorization, and workflow tests (see "Known
  verification limitation" below for what is *not* yet compiled/tested in this environment),
- **Phase 4 (Collaboration) and Phase 5 (Attachments and Kanban board) are both fully complete**, closing
  out Milestone 2: fixed emoji reactions, @mention handles and in-app notifications, the Markdown editor
  (toolbar/live preview/full upload+drag-drop+paste attachment support), simplified worklogs, the
  admin/security audit log, ad-hoc issue filter/search widening, the personal dashboard, Kanban board WIP
  limits, and the full attachments vertical (local filesystem storage, four native-element previews,
  sortable list, recycle bin) — see the batch-by-batch history below and `docs/VERIFICATION.md` for exactly
  what was built and verified in each.

Authoritative documents:

- [REDUCED_SCOPE_SPECIFICATION.md](REDUCED_SCOPE_SPECIFICATION.md) — the current V1 product baseline,
- [docs/REDUCED_SCOPE_DECISIONS.md](docs/REDUCED_SCOPE_DECISIONS.md) — the current V1 decision register,
- [docs/REMOVED_AND_DEFERRED_FEATURES.md](docs/REMOVED_AND_DEFERRED_FEATURES.md) — what is intentionally not being built,
- [docs/REDUCED_SCOPE_ROADMAP.md](docs/REDUCED_SCOPE_ROADMAP.md) — the current phased implementation plan,
- [docs/REDUCED_SCOPE_ARCHITECTURE.md](docs/REDUCED_SCOPE_ARCHITECTURE.md) — the current module/runtime architecture,
- [docs/REDUCED_SCOPE_DATA_MODEL.md](docs/REDUCED_SCOPE_DATA_MODEL.md) — the current target tables,
- [docs/SCHEMA.md](docs/SCHEMA.md) — schema currently implemented,
- [SPECIFICATION.md](SPECIFICATION.md) / [docs/PRODUCT_DECISIONS_COMPLETE.md](docs/PRODUCT_DECISIONS_COMPLETE.md) / [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) / [docs/DATA_MODEL.md](docs/DATA_MODEL.md) / [docs/ROADMAP.md](docs/ROADMAP.md) — the original full-scope plan, kept only as long-term reference,
- [docs/HANDOFF_NOTES.md](docs/HANDOFF_NOTES.md) — continuation context for the next coding agent.

## Architecture today

```text
Browser (semantic HTML + vanilla JS enhancement)
        |
        v
Crow HTTP/JSON routes  --(session cookie / CSRF)-->  TicketHub::Application::AuthService
        |                                                       |
        v                                                       v
TicketHub::Application::TicketService  <--(Principal)--  IDatabase (users, sessions, local_credentials)
        |
        v
TicketHub::Infrastructure::Database::IDatabase
        |                         |
        v                         v
PostgresDatabase (libpq)    SqliteDatabase (sqlite3)
```

The reduced-scope V1 target (`docs/REDUCED_SCOPE_ARCHITECTURE.md`) keeps exactly one database port with
two implementations and drops the speculative multi-backend ports (search/mail/jobs/events/cache/
secrets/observability) that the original full-scope architecture reserved.

## Requirements

- CMake 3.25+
- C++20 compiler
- SQLite development files when `TICKETHUB_WITH_SQLITE=ON`
- PostgreSQL client development files when `TICKETHUB_WITH_POSTGRES=ON`
- libargon2 development files (local password hashing)
- Crow 1.3.3 for the server target

Typical Debian dependencies:

```bash
sudo apt install build-essential cmake libpq-dev libsqlite3-dev libargon2-dev libasio-dev
```

## Build and test the core without downloading Crow

```bash
cmake -S . -B build-core \
  -DTICKETHUB_BUILD_SERVER=OFF \
  -DTICKETHUB_WITH_POSTGRES=ON \
  -DTICKETHUB_WITH_SQLITE=ON
cmake --build build-core --parallel 4
ctest --test-dir build-core --output-on-failure
```

## Build the server

```bash
cmake -S . -B build \
  -DTICKETHUB_BUILD_SERVER=ON \
  -DTICKETHUB_WITH_POSTGRES=ON \
  -DTICKETHUB_WITH_SQLITE=ON
cmake --build build --parallel 4
```

CMake first searches for an installed `Crow::Crow`; otherwise it fetches the pinned Crow tag. `vcpkg.json` can provide Crow, libpq and SQLite.

## Administration CLI

The CLI builds without Crow and uses the same database adapters:

```bash
./build-core/ticket-hub-cli version
./build-core/ticket-hub-cli diagnostics
TICKETHUB_DB_DRIVER=sqlite ./build-core/ticket-hub-cli migrate
TICKETHUB_DB_DRIVER=sqlite ./build-core/ticket-hub-cli seed-demo
TICKETHUB_DB_DRIVER=sqlite ./build-core/ticket-hub-cli create-user "person@example.com" "A Person" "a sufficiently long password" [--admin] [--handle=<handle>]
TICKETHUB_DB_DRIVER=sqlite ./build-core/ticket-hub-cli backup ./backups/2026-08-02
TICKETHUB_DB_DRIVER=sqlite ./build-core/ticket-hub-cli restore ./backups/2026-08-02 --yes
```

`diagnostics` redacts the PostgreSQL connection string. Installed deployments should set `TICKETHUB_MIGRATIONS_ROOT` to the installed migration directory when it differs from the compiled development default.

`backup`/`restore` (D106-D108, Phase 7) are offline/maintenance-window operations -- stop the server first;
neither command checks whether it is still running. `backup <output-directory>` copies the attachments
directory and dumps the database (SQLite: the online backup API, correct regardless of WAL/checkpoint
state; PostgreSQL: `pg_dump --clean --if-exists`, so the dump is self-contained for a direct restore) into
`<output-directory>`, refusing to write into a directory that already exists and is non-empty.
`restore <backup-directory> --yes` permanently overwrites the current database and attachments directory
with the backup's contents and then runs pending migrations (SQLite: the online backup API in reverse;
PostgreSQL: `psql -v ON_ERROR_STOP=1`) -- the `--yes` flag is mandatory; without it the command prints a
warning and refuses to proceed. There is no isolated staging environment and no manifest/checksum file
(D106-D108); the admin is responsible for their own pre-restore backup of whatever is about to be
overwritten. `ticket-hub-cli migrate` remains the entire upgrade mechanism (D111) -- already implemented,
no separate upgrade command.

`create-user` is administrator-only account creation: there is no public registration and no invitation
flow in V1 (`REDUCED_SCOPE_SPECIFICATION.md` section 3). The password is set directly by whoever runs
the command; there is no forced-change-on-first-login flow. `--handle` sets the optional, unique @mention
handle (D56/D80) -- there is no self-service profile-editing flow yet to set or change it afterward.

## Run with SQLite

```bash
TICKETHUB_DB_DRIVER=sqlite \
TICKETHUB_SQLITE_PATH=./ticket-hub.db \
./build/ticket-hub
```

Open `http://127.0.0.1:8080`.

SQLite has the same planned user-facing feature set, but only one Ticket Hub server process and limited worker concurrency.

## Run with PostgreSQL

```bash
docker compose up -d postgres

TICKETHUB_DB_DRIVER=postgres \
TICKETHUB_DATABASE_URL='host=127.0.0.1 port=5432 dbname=tickethub user=tickethub password=tickethub-dev' \
./build/ticket-hub
```

Do not put production secrets in shell history. The target product uses a pluggable secrets backend.

## Run with Docker

The official Docker image plus Docker Compose is the only supported distribution path for V1 (D50) --
no `.deb`/`.rpm`, no Helm/Kubernetes:

```bash
docker compose up
```

This builds the image from the `Dockerfile` in this repository (a two-stage build: full toolchain to
compile, then a slim runtime image with just the shared libraries the binary links against), starts
PostgreSQL, waits for it to report healthy, then starts Ticket Hub against it -- reachable at
`http://127.0.0.1:8080`. A fresh instance ships with no accounts at all (D2: administrator-created
accounts only, no public registration); create the first admin with:

```bash
docker compose exec ticket-hub ticket-hub-cli create-user "you@example.com" "Your Name" "a sufficiently long password" --admin
```

Or set `TICKETHUB_SEED_DEMO=true` for the `ticket-hub` service in `docker-compose.yml` to get the same
demo data/logins used throughout this README instead. The `ticket-hub-attachments` named volume
(mounted at `/data` in the container, `TICKETHUB_ATTACHMENTS_DIR=/data/attachments`) and
`ticket-hub-postgres` volume persist data across `docker compose down`/`up` cycles;
`docker compose down -v` removes both.

`docker compose up -d postgres` (starting only that one service) continues to work exactly as in "Run
with PostgreSQL" above, for building from source and running the database in Docker only.

**Verification note:** this Docker packaging was written and validated as far as this development
environment's network policy allows -- `docker build --check` and `docker compose config` both pass
cleanly (Dockerfile syntax/best-practices and Compose YAML schema), and the exact runtime configuration
the container sets (`TICKETHUB_WEB_ROOT`/`TICKETHUB_MIGRATIONS_ROOT`/`TICKETHUB_ATTACHMENTS_DIR` pointed
at a `cmake --install`-produced tree, against both a fresh SQLite file and a live PostgreSQL database
using the exact `host=... dbname=... user=... password=...` connection-string shape the Compose file
uses) was verified directly on the host and confirmed fully working. The actual `docker build` of the
image itself -- pulling the `debian:bookworm-slim` base layers -- could not be executed in this specific
sandboxed environment: the base-image blob pull is blocked by this session's network egress policy
(confirmed via the proxy's own status log: a policy-level `403` on the CDN host Docker Hub redirects
image-layer downloads to, not a bug in the Dockerfile). This is a report-don't-work-around situation per
this environment's own operating rules, not a Ticket Hub defect -- the same `docker compose up` should
build and run normally in any environment with ordinary Docker Hub network access.

## Configuration currently implemented

| Variable | Default | Meaning |
|---|---|---|
| `TICKETHUB_DB_DRIVER` | `postgres` | `postgres`, `postgresql`, or `sqlite` |
| `TICKETHUB_DATABASE_URL` | local `tickethub` DB | libpq connection string |
| `TICKETHUB_SQLITE_PATH` | `./ticket-hub.db` | SQLite file |
| `TICKETHUB_BIND_ADDRESS` | `127.0.0.1` | HTTP bind address |
| `TICKETHUB_PORT` | `8080` | HTTP port |
| `TICKETHUB_AUTO_MIGRATE` | `true` | discover/apply schema migrations |
| `TICKETHUB_SEED_DEMO` | `true` | apply idempotent demo data |
| `TICKETHUB_WEB_ROOT` | source `web/` | static web root |
| `TICKETHUB_MIGRATIONS_ROOT` | source `migrations/` | backend migration root |
| `TICKETHUB_ATTACHMENTS_DIR` | source `data/attachments/` | local filesystem attachment storage root (D15) -- point this at a persistent, backed-up volume in a real deployment |

## API

The API now lives under a formal `/api/v1` prefix (D127) -- every route below except `GET /api/health`
(kept unversioned, following the common convention that infra/monitoring health checks live outside API
versioning; not specified by any decision text, a conservative choice documented here explicitly).
`GET /api/v1/issues` supports numbered/offset pagination (D126): optional `page` (1-based, default 1) and
`pageSize` (default and max 200, D125's "max page size" -- no admin exceptions) query parameters. The
response envelope is `{"items": [...], "page", "pageSize", "totalItems", "totalPages"}`; a caller that
sends neither parameter gets exactly the same result set the route always returned (the pre-existing,
previously undocumented 200-row cap this fixes the silent-truncation transparency of), now with an
honest `totalItems` so a caller can tell whether more rows exist. No other list endpoint is paginated yet
-- this is a deliberate, documented partial rollout of D126, not full coverage. Every JSON request body
is capped at 1 MiB (`413` if exceeded) and every bulk-action `issueKeys` array is capped at 200 items
(`400` if exceeded) -- fixed constants, no admin configuration, per D125. PAT authentication (D39/D40) is
already live: every
route below marked "session" also accepts an `Authorization: Bearer <token>` header from a personal
access token instead -- the two are mutually exclusive per request (D54), and a Bearer-authenticated
write needs no `X-CSRF-Token` header (CSRF only defends against a browser silently attaching a session
cookie, which a PAT is never subject to).

Fixed rate limits (D124/D125) are also already live and apply to every route in the table below:
`/api/v1/auth/login` is capped at 20 attempts per IP per 15 minutes (on top of the existing per-account
10-attempts/15-minutes lockout below), and every write route (POST/PUT/PATCH/DELETE) shares a 120-
requests-per-minute cap keyed by user id when authenticated, else by IP. Both return `429` with a JSON
`{"error": "..."}` body and a `Retry-After` header (in seconds, matching that limiter's fixed window) on
trip; there is no admin configuration for either limit.

| Method | Route | Auth required | Purpose |
|---|---|---|---|
| `GET` | `/api/health` | no | health, version and backend |
| `POST` | `/api/v1/auth/login` | no | `{email,password}` → sets session + CSRF cookies |
| `POST` | `/api/v1/auth/logout` | no | clears session (safe to call unauthenticated) |
| `GET` | `/api/v1/auth/me` | session or PAT | current principal, or 401 |
| `GET` | `/api/v1/tokens` | session | list the caller's own personal access tokens (D39/D40) |
| `POST` | `/api/v1/tokens` | session + CSRF | `{name, expiresInDays}` → `{token, ...}`; raw token shown only once |
| `DELETE` | `/api/v1/tokens/{id}` | session + CSRF | revoke one of the caller's own tokens |
| `GET` | `/api/v1/sessions` | session (not PAT) | the caller's own active web sessions, with `isCurrent` marked (D54) |
| `POST` | `/api/v1/sessions/sign-out-others` | session + CSRF (not PAT) | signs out every other session for the caller, keeps the current one |
| `GET` | `/api/v1/dashboard` | session, or anon if enabled | counts, recent issues, and (authenticated only, D24) assigned-to-me/watched/upcoming-deadline issues |
| `GET` | `/api/v1/board-columns` | session, or anon if enabled | one entry per fixed workflow status with its optional soft WIP limit (D32/D33) |
| `PUT` | `/api/v1/board-columns/{statusKey}` | session + CSRF, global admin | `{wipLimit}` (number or null); installation-wide, not per-project |
| `GET` | `/api/v1/users` | session | user directory (id/displayName/email/handle) for @mention autocomplete (D80) |
| `GET` | `/api/v1/notifications` | session | `?unread=true` filters; fixed set (D14) |
| `GET` | `/api/v1/notifications/unread-count` | session | `{count}` |
| `POST` | `/api/v1/notifications/{id}/read` | session + CSRF | scoped to the caller's own notifications |
| `POST` | `/api/v1/notifications/read-all` | session + CSRF | scoped to the caller's own notifications |
| `GET` | `/api/v1/admin/audit-events` | session, global admin | newest 200 admin/security events (D23) |
| `GET` | `/api/v1/projects` | session, or anon if enabled | active project summaries |
| `POST` | `/api/v1/projects` | session + CSRF, global admin | create project |
| `PATCH` | `/api/v1/projects/{key}/archived` | session + CSRF, project admin | `{archived}` |
| `DELETE` | `/api/v1/projects/{key}` | session + CSRF, project admin | move to recycle bin |
| `GET` | `/api/v1/projects/deleted` | session, global admin | list recycle bin |
| `POST` | `/api/v1/projects/{key}/restore` | session + CSRF, global admin | restore from recycle bin |
| `DELETE` | `/api/v1/projects/{key}/permanent` | session + CSRF, global admin | permanently delete |
| `GET` | `/api/v1/settings/anonymous-read` | session | current toggle value |
| `PUT` | `/api/v1/settings/anonymous-read` | session + CSRF, global admin | `{enabled}` |
| `GET` | `/api/v1/settings/latest-known-version` | session, global admin | `{currentVersion, latestKnownVersion, updateAvailable}` (D112) |
| `PUT` | `/api/v1/settings/latest-known-version` | session + CSRF, global admin | `{version}` -- sets what the admin banner compares against |
| `GET` | `/api/v1/issues` | session, or anon if enabled | filter by `project`, `status`, `type`, `priority`, `assignee`, `label`, `dueBefore`, `q` (ad-hoc only, D10/D43; `q` also matches description); paginated via `page`/`pageSize` (D126) |
| `GET` | `/api/v1/issues/export.csv` | session, or anon if enabled | read-only CSV export of issues (D48), same filters as above |
| `POST` | `/api/v1/issues` | session + CSRF, project member | create issue (`assigneeEmail`, `parentIssueKey`) |
| `GET` | `/api/v1/issues/{key}` | session, or anon if enabled | current key or permanent alias |
| `PATCH` | `/api/v1/issues/{key}` | session + CSRF, project member | full-replacement edit (D129); see below |
| `PATCH` | `/api/v1/issues/{key}/status` | session + CSRF, project member | `{statusKey, resolution?, expectedVersion?}` |
| `GET` | `/api/v1/issues/{key}/comments` | session, or anon if enabled | live comments |
| `POST` | `/api/v1/issues/{key}/comments` | session + CSRF, project member | add comment |
| `PATCH` | `/api/v1/issues/{key}/comments/{id}` | session + CSRF, author or project admin | `{body, expectedVersion?}` — full-replacement edit (D81) |
| `DELETE` | `/api/v1/issues/{key}/comments/{id}` | session + CSRF, author or project admin | tombstone delete (D82) |
| `GET` | `/api/v1/issues/{key}/comments/{id}/reactions` | session, or anon if enabled | current reactions |
| `POST`/`DELETE` | `/api/v1/issues/{key}/comments/{id}/reactions/{key}` | session + CSRF | react/un-react (no project role required, D84) |
| `GET` | `/api/v1/issues/{key}/worklogs` | session, or anon if enabled | logged time entries |
| `POST` | `/api/v1/issues/{key}/worklogs` | session + CSRF, project member | `{workDate, timeSpentSeconds, comment?}` (D12/D13) |
| `PATCH` | `/api/v1/issues/{key}/worklogs/{id}` | session + CSRF, project member | full-replacement edit, no own-vs-others split |
| `DELETE` | `/api/v1/issues/{key}/worklogs/{id}` | session + CSRF, project member | tombstone delete, no own-vs-others split |
| `GET` | `/api/v1/issues/{key}/attachments` | session, or anon if enabled | active attachments (D15/D98-D105) |
| `POST` | `/api/v1/issues/{key}/attachments` | session + CSRF, project member | `multipart/form-data`, one `file` part; fixed 25MB/20-per-issue limits, D98 |
| `DELETE` | `/api/v1/issues/{key}/attachments/{id}` | session + CSRF, uploader or project admin | tombstone delete (D101) |
| `GET` | `/api/v1/attachments/{id}/download` | session, or anon if enabled | raw bytes with `Content-Type`/`Content-Disposition`; not nested under `/issues/{key}`, since a download/preview URL only ever needs the id |
| `GET` | `/api/v1/attachments/deleted` | session, global admin | recycle bin, 90-day on-demand retention (D102) |
| `POST` | `/api/v1/attachments/{id}/restore` | session + CSRF, global admin | restore from recycle bin |
| `DELETE` | `/api/v1/attachments/{id}/permanent` | session + CSRF, global admin | permanently delete (and its file) |
| `POST` | `/api/v1/issues/{key}/clone` | session + CSRF, project member | simple field-copy clone (D60) |
| `POST` | `/api/v1/issues/{key}/reorder` | session + CSRF, project member | `{beforeIssueKey?}` — manual ordering (D31) |
| `POST` | `/api/v1/issues/{key}/move` | session + CSRF, member of both projects | `{targetProjectKey}` — move to another project (D37) |
| `GET` | `/api/v1/issues/{key}/links` | session, or anon if enabled | links from both ends |
| `POST` | `/api/v1/issues/{key}/links` | session + CSRF, member of both projects | `{targetIssueKey, linkType}` |
| `DELETE` | `/api/v1/issue-links/{id}` | session + CSRF, member of both projects | remove a link |
| `GET` | `/api/v1/issues/{key}/watchers` | session, or anon if enabled | current watchers |
| `POST`/`DELETE` | `/api/v1/issues/{key}/watch` | session + CSRF | watch/unwatch (no project role required) |
| `GET` | `/api/v1/issues/{key}/voters` | session, or anon if enabled | current voters |
| `POST`/`DELETE` | `/api/v1/issues/{key}/vote` | session + CSRF | vote/unvote (no project role required) |
| `DELETE` | `/api/v1/issues/{key}` | session + CSRF, project admin | move issue to recycle bin |
| `GET` | `/api/v1/issues/deleted` | session, global admin | list issue recycle bin |
| `POST` | `/api/v1/issues/{key}/restore` | session + CSRF, global admin | restore issue from recycle bin |
| `DELETE` | `/api/v1/issues/{key}/permanent` | session + CSRF, global admin | permanently delete issue |
| `POST` | `/api/v1/issues/bulk/status` | session + CSRF, project member per issue | `{issueKeys[], statusKey, resolution?}` |
| `POST` | `/api/v1/issues/bulk/assign` | session + CSRF, project member per issue | `{issueKeys[], assigneeEmail?}` |
| `POST` | `/api/v1/issues/bulk/label` | session + CSRF, project member per issue | `{issueKeys[], label}` |
| `POST` | `/api/v1/issues/bulk/delete` | session + CSRF, project admin per issue | `{issueKeys[]}` |

Every `POST /api/v1/issues/bulk/*` route returns `{succeeded: [...], failed: [...]}` — issue keys, not a
single status code — since each key is authorized and processed independently and a partial failure
(unknown key, insufficient role for that particular issue, a workflow-rule violation) does not roll back
the keys that already succeeded.

`PATCH /api/v1/issues/{key}` is a full-replacement edit, not a JSON-merge-patch: `{summary, description?,
priorityKey, issueTypeKey, parentIssueKey?, assigneeEmail?, storyPoints?, dueDate?, labels?,
expectedVersion?}`. Every editable field is always the caller's intended final value (e.g. omitting
`assigneeEmail` unassigns the issue, it does not leave the current assignee alone) -- the caller is
expected to pre-populate the request from the current issue. `issueTypeKey`/`parentIssueKey` re-typing/
re-parenting (post-V1 follow-up) re-validates the fixed hierarchy shape exactly like `POST /api/v1/issues`
does at creation, plus rejects self-parenting; retyping across hierarchy levels (Epic <-> Story/Task/Bug
<-> Sub-task) is additionally rejected while the issue currently has child issues (checked transactionally
inside the database layer, since it depends on concurrent state) -- same-level retyping (e.g. Task -> Bug)
is always allowed.

Issue responses include `version` and `resolution`. A stale `expectedVersion` returns HTTP 409. A
missing/insufficient project role or global-admin requirement returns HTTP 403. An anonymous read while
the toggle is off returns HTTP 401. A request that violates the fixed workflow's hardcoded rules --
completing an issue without a `resolution`, an unrecognized `resolution`, or completing an issue that
still has an unfinished sub-task -- returns HTTP 422. `parentIssueKey` on issue creation is validated
against the fixed Epic/Sub-task hierarchy (a Sub-task requires a same-project Story/Task/Bug parent, an
Epic may not have one, a Story/Task/Bug's optional parent must be a same-project Epic); a violation
returns HTTP 400, same as any other invalid request field.

`POST /api/v1/issues/{key}/reorder` moves the issue to immediately before `beforeIssueKey` (which must be in
the same project), or to the end of the project if `beforeIssueKey` is omitted/null; the response is the
reordered issue, with the whole project's `rankOrder` values renumbered in one pass. `POST
/api/v1/issues/{key}/move` moves the issue to `targetProjectKey`, allocating a new key/number there; the
vacated key becomes a permanent alias (`GET /api/v1/issues/{oldKey}` keeps resolving to it). Both return
HTTP 400 for an unknown/cross-project anchor, an unknown target project, moving to the issue's current
project, or moving an issue that has a parent or any children.

`linkType` on `POST /api/v1/issues/{key}/links` must be one of the fixed catalog (`blocks`, `relates_to`,
`duplicates`, `clones`); there is no admin-configurable link-type list. Both the source and target
issue's projects must be accessible to the actor (project-Member-or-above), not just the source's.

`PATCH /api/v1/issues/{key}/comments/{id}` is a full-replacement edit of `body` (D81), sharing the same
`expectedVersion`/409 optimistic-locking contract as issue edits, and sets an `editedAt` timestamp on the
response -- there is no stored history of the comment's prior text, just the fact that it was edited.
`DELETE /api/v1/issues/{key}/comments/{id}` is a tombstone delete (D82): the row and original body stay in
the database, simply excluded from `GET /api/v1/issues/{key}/comments` afterward -- there is no separate
recycle-bin API for comments, unlike issues and projects. Permissions on both are simplified (D83): the
comment's own author may always edit/delete it; otherwise the actor needs project-Admin-or-above (or
global admin) — not the edit-own/edit-all/delete-own/delete-all matrix the original spec described.

The watch/vote routes are the one exception among issue writes: they require only an authenticated
session, not project-Member-or-above, since watching/voting is self-referential and doesn't mutate the
issue itself. `POST` is idempotent (watching/voting twice is a no-op, still `200`); `DELETE` on a watch/
vote that doesn't exist is also `200`, not `404`.

`{key}` in `POST`/`DELETE /api/v1/issues/{key}/comments/{id}/reactions/{key}` is a path segment from the
fixed eight-reaction catalog (D84: `thumbs_up`, `thumbs_down`, `laugh`, `hooray`, `confused`, `heart`,
`rocket`, `eyes` -- GitHub's well-known reaction set, chosen as a conservative default since the decision
register calls for "a fixed reaction set" without enumerating one). Reactions are self-service like
watch/vote (no project-role check), and each user may add each reaction key at most once per comment;
`POST`/`DELETE` are idempotent the same way watch/vote are. `GET .../reactions` returns
`{items: [{reactionKey, user}, ...]}` -- the caller groups by `reactionKey` for counts/highlighting, the
same "server stays dumb, client aggregates" split used for issue links.

`GET /api/v1/users` is a directory listing (id/displayName/email/handle only -- no isAdmin/active/timeZone),
requiring a session even when the installation-wide anonymous-read toggle is on, since the user directory
is more sensitive than issue data. It backs @mention autocomplete (D80) and is the only way the demo UI
discovers handles.

The fixed in-app notification set (D14) is created as a side effect of three existing writes, never
directly by an API caller: `POST`/`PATCH /api/v1/issues` notifies a newly-set or changed assignee (skipping
self-assignment and a no-op re-save with the same assignee); `POST /api/v1/issues/{key}/comments` notifies
every `@handle` mention resolved in the body (D80) and every watcher of the issue except the comment's
own author, with mentioned taking priority over watched for a recipient who is both (one notification,
not two). `GET /api/v1/notifications` and `GET /api/v1/notifications/unread-count` are always scoped to the
caller's own notifications; `POST /api/v1/notifications/{id}/read` returns `{ok: false}` rather than 404 for
an unknown id or someone else's notification (there is no cross-user notification management, so there is
nothing more specific to report). Mentions are parsed only when a comment is created, not on every edit,
to avoid re-notifying on every save of an already-mentioning comment.

Session-authenticated writes require the `X-CSRF-Token` header to match the readable `th_csrf` cookie
set at login (double-submit pattern) — see `src/web/Api.cpp`. **This file has now been compiled and
smoke-tested against a live server** (see "Server verification" below).

The demo UI now has a login screen (`web/index.html`/`app.js`): on load it silently probes
`GET /api/v1/auth/me`; if that returns 401 it shows a sign-in form instead of the app shell. A successful
`POST /api/v1/auth/login` reveals the app shell and shows the signed-in user's name/email/initials in the
sidebar footer, alongside a sign-out button (`POST /api/v1/auth/logout`) that returns to the login screen.
Every non-`GET` request the UI makes now reads the `th_csrf` cookie and attaches it as `X-CSRF-Token`
automatically, and any `401` response from any API call redirects back to the login screen (handles the
session expiring mid-use). Browser-verified end-to-end with Playwright/Chromium against
`127.0.0.1` — see "Server verification" below; Chromium (and other major browsers) treat `localhost`/
`127.0.0.1` as a "potentially trustworthy origin", so the session/CSRF cookies' `Secure` attribute does
not block local HTTP testing, while still requiring real TLS for any other hostname in production.

## Server verification

For most of this project's history, the `ticket-hub` server target (Crow-based) could not be built in the
authoring sandbox because outbound access to `github.com` — needed to fetch Crow via CMake
`FetchContent` — was blocked by the sandbox's network egress policy (the same limitation recorded for the
original prototype in `handoff/IMPLEMENTATION_STATE.md`). **That is no longer the case**: network access
to `github.com` became reachable, and the server target has now been built and smoke-tested end-to-end
against a live HTTP server. Crow 1.3.3 additionally needs standalone `asio` (`sudo apt-get install
libasio-dev`, already listed under "Requirements" above) — install it if `find_package(asio)` fails
during configure.

`src/main.cpp`, `src/web/Api.cpp`, and `src/web/HttpServer.cpp` compiled with **zero warnings or errors
from Ticket Hub's own code** (Crow's own headers emit a large number of `-Wconversion` warnings under
`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`; that's third-party noise, not addressable here). Every
route in the "Prototype API" table above — spanning all three completed phases — was then exercised live
with `curl` against a running instance: login/logout/session validation, CSRF enforcement (missing token
→ 403), project-role enforcement (non-member → 403), the fixed workflow's resolution-required/cleared and
409-conflict rules, full-replacement edit, cloning, issue links, watching/voting, the issue recycle bin,
all four bulk actions, comments, the full project lifecycle, the anonymous-read-access toggle, and the two
newest routes — `POST /api/v1/issues/{key}/reorder` and `POST /api/v1/issues/{key}/move` — including confirming
a moved issue's vacated key still resolves via `issue_key_aliases` through the real HTTP/JSON layer.
**Zero bugs were found** in `Api.cpp` across this sweep — every route, written blind against established
patterns over many prior batches, behaved exactly as documented on the first real test. Full detail is in
`docs/VERIFICATION.md`'s "Server target verified end-to-end" entry.

A follow-up batch then added the login screen described above and verified it with a real, automated
browser (Playwright/Chromium, headless) rather than `curl`: fresh page load shows the login screen and
hides the app shell; signing in with valid credentials shows the app shell, the correct user's name, and
lets every view (dashboard/board/issues/projects) render; creating an issue and changing its status both
succeed (confirming the browser's own `fetch` calls carry the CSRF header correctly, not just `curl` with
a manually-added header); signing out clears both cookies and returns to the login screen, and a page
reload afterward stays on the login screen rather than silently re-entering the app; a wrong password
shows an inline error without ever revealing the app shell. Full detail is in `docs/VERIFICATION.md`.

A follow-up batch then added an Epic/parent picker to the create-issue modal and an inline resolution
picker to the drawer's status control — previously completing an issue via the UI always failed with 422
since `resolution` was never sent, and there was no way to set `parentIssueKey` at all. Both were
browser-verified the same way: creating an Epic then a Story with that Epic as parent (drawer links to
it correctly); a parentless Sub-task showing the server's exact validation message inline; completing an
issue showing/applying the resolution picker; reopening clearing the resolution and hiding the picker for
that direction. A race condition in the picker's own async refresh (caught by this same browser test) was
fixed with a request-id guard. Full detail is in `docs/VERIFICATION.md`.

A third follow-up batch then added full edit, clone, links, and watch/vote to the issue drawer — an
actions row (Watch/Vote toggles with live counts, Clone, Edit) and a Links section (list with correct
bidirectional labels, add form, delete). Browser-verified the same way: watch/vote toggling and
reverting correctly; cloning navigating to the new issue, whose Links section already shows the automatic
`clones` link (D60); adding and deleting a link, confirmed bidirectional (visible and deletable from
either linked issue); a full edit saving correctly and a cancelled edit discarding its changes. This
testing caught a genuine CSS layout bug — a link row could overflow into the drawer's sidebar column and
block clicks on whatever sat underneath it there (`.link-list`, a CSS grid container, was letting its
items claim their full content width instead of shrinking) — fixed with an explicit `min-width: 0`.

The issue drawer now covers the full single-issue lifecycle. A fourth follow-up batch then added
project-management UI: a "New project" modal, per-card Archive/Unarchive and Delete buttons, and a
recycle-bin view (visible and usable only for global administrators, matching D88). Browser-verified the
same way, including a non-admin seeing neither the recycle-bin toggle nor a working create-project action
(inline 403). This testing also caught a real state-management bug — `state` (current view, selected
project, filters) was never reset on logout, so a second user in the same browser tab could land on
whatever the first user last had open, including a project they can't access or one just
archived/deleted — fixed by resetting all of `state` on every login-screen transition, not just the
signed-in user.

A fifth follow-up batch then added the issue recycle bin, symmetric to the project one: a Delete button in
the drawer's actions row, and a recycle-bin toggle in the Issues view (global-admin-only, with
Restore/Delete-permanently per row). Browser-verified the same way, including a project-role-insufficient
delete attempt failing with the server's exact 403 message rather than silently succeeding.

A sixth and final follow-up batch added manual reordering (an Order column with move-up/move-down buttons
on the Issues table, shown only with a single project selected, since the reorder anchor must be in the
same project), moving an issue to another project (a picker in the drawer), and simple bulk actions
(checkboxes plus a bulk-action bar for status/assign/label/delete). Browser-verified the same way,
including confirming the new checkboxes and reorder buttons don't also open the issue drawer despite
living inside the same clickable table row (`event.stopPropagation()`, caught and fixed proactively during
implementation rather than by a failing test).

With this, `web/` covers every write route added across Phases 1-3 -- there is no remaining gap between
what the API exposes and what the demo UI can reach.

A seventh batch started Phase 4 (Collaboration): comment editing and tombstone delete (D81/D82/D83), the
first Phase 4 feature. Added `IDatabase::editComment`/`deleteComment`/`findCommentById` in both adapters
(migration `008_comment_editing.sql` adds `comments.edited_at`), matching `TicketService` methods with
simplified author-or-project-admin permissions, the two new API routes above, and Edit/Delete controls in
the drawer's comment list (shown only for the comment's author or a global admin -- a client-side
simplification, not the actual security boundary, since the client never loads per-project role
information the way it would need to for a project-admin-but-not-author case). Browser-verified: adding,
editing (with an "(edited)" marker appearing), cancelling an in-progress edit (discards the change), and
deleting a comment all work through the real HTTP layer; a non-author, non-global-admin actor sees no
Edit/Delete buttons on someone else's comment at all (the client-side simplification above), and the
authorization tests separately confirm the server itself also rejects such an attempt with 403.

An eighth batch added the next Phase 4 feature: fixed emoji reactions on comments (D84). Added
`comment_reactions` (migration `009_comment_reactions.sql`, a three-column composite-key many-to-many
table mirroring `issue_watchers`/`issue_votes`) with `IDatabase::addCommentReaction`/
`removeCommentReaction`/`listCommentReactions` in both adapters, matching `TicketService` methods with
the same self-service/no-project-role reasoning as watch/vote, and the two new API routes above. Since
the decision register calls for "a fixed reaction set" without naming one, this uses GitHub's own
well-known eight-reaction set (`thumbs_up`, `thumbs_down`, `laugh`, `hooray`, `confused`, `heart`,
`rocket`, `eyes`) as a conservative, familiar default -- documented explicitly as a filled product-decision
gap. `web/` renders all eight as small pill buttons under each comment, showing a per-reaction count and
highlighting the ones the current viewer has added; clicking toggles react/un-react through the real HTTP
layer. Browser-verified: reacting shows the button go active with a "1" count; clicking again removes it
(button reverts, count disappears); a second, different reaction key can coexist with an active one; and,
switching to a second user, the count is shared (visible to both) while each user's own "active" highlight
is independent -- confirmed by alex reacting to a comment demo had already reacted to and the count going
from 1 to 2 without alex seeing demo's own active state carried over.

A ninth batch added @mention handles and the fixed in-app notification set (D56/D80/D14). Migration
`010_mentions_and_notifications.sql` adds `users.handle` (optional, unique via a partial index) and
`notifications` (`user_id`, `type`, `issue_id` nullable, `read_at` -- the exact minimal shape in
`docs/REDUCED_SCOPE_DATA_MODEL.md`). `ticket-hub-cli create-user` gained `--handle=<handle>`; the three
seeded demo accounts now have handles (`demo`/`alex`/`sam`). `TicketService::createIssue`/`editIssue`
notify a newly-set or changed assignee; `addComment` parses `@handle` tokens out of the body (once, at
creation) and notifies each resolved user, plus every watcher of the issue except the comment's own
author -- a recipient who is both mentioned and watching gets exactly one notification, the more specific
reason winning. New `GET /api/v1/users` (directory listing for autocomplete) and the four
`/api/v1/notifications*` routes above. `web/` gained a notification bell with an unread-count badge in the
top bar (clicking a notification marks it read and opens the related issue, with a "mark all read"
button) and an @mention autocomplete dropdown under the comment textarea (both add and edit), backed by
the cached `/api/v1/users` directory. Browser-verified end-to-end: creating an issue assigned to a second
user shows exactly one unread notification for them; typing `@sa` in the comment box shows a matching
autocomplete suggestion that inserts the full handle on click; posting a comment that mentions a user
notifies them with the correct issue reference; opening the notification panel, clicking an item, and
using "mark all read" all update the badge correctly through the real HTTP layer; and a full regression
re-run of the eighth batch's reaction test and the seventh batch's comment-editing test both still pass
unchanged.

A tenth batch closed out D16 (rich text): comment bodies and issue descriptions now render as formatted
Markdown instead of plain escaped text, with a visual toolbar and a live preview toggle on every
Markdown-capable textarea (comment add, comment edit, issue description on create and edit) -- entirely
in `web/`, no schema or API change, since bodies are still stored and transmitted as raw Markdown text.
`renderMarkdown`/`renderMarkdownInline` implement a deliberately small subset (bold, italic, inline code,
links, headings, lists, blockquotes, fenced code, horizontal rules), safe by construction: the raw text
is HTML-escaped *first* (the same `escapeHtml` used everywhere else in `web/`), and every transform after
that only ever wraps the already-escaped text in a fixed, hardcoded set of tags -- user input can never
introduce a real HTML tag or attribute this way, so there is no separate sanitization pass that could be
wrong. Link targets are restricted to `http(s)`/`mailto`; any other scheme (`javascript:`, etc.) is left
as literal `[text](url)` text rather than becoming a clickable link. Browser-verified, including two
security-focused checks: a comment body containing `<script>...</script>` and an `onerror`-bearing `<img>`
tag renders as inert, visible literal text (confirmed via a page-level flag that the payload never
executes) rather than as markup, and a `[label](javascript:alert(1))` link renders as literal bracket-
paren text rather than a clickable anchor. Caught and fixed one real rendering bug during implementation,
before it reached a security concern: the first cut of the italic regex used `_..._` as an alternative to
`*...*`, which mishandled text containing two separate double-underscore identifiers (e.g.
`__init__`-style names) by treating an underscore from the *first* pair and one from the *second* pair as
matching open/close delimiters, silently swallowing everything in between into a single (still safely
escaped, just visually wrong) `<em>` span -- fixed by dropping underscore-delimited emphasis entirely and
supporting only `**bold**`/`*italic*`, which have no such adjacency ambiguity. Also verified no regression
in the eighth batch's reaction test, the seventh batch's comment-editing test, and the ninth batch's
mentions/notifications test (unaffected by the `.comment-body-text` markup changing from `<p>` to `<div>`
to legally contain the new block-level Markdown output).

An eleventh batch added simplified worklogs (D12/D13). Migration `011_worklogs.sql` adds `worklogs`
(`id`, `issue_id`, `author_user_id`, `work_date`, `time_spent_seconds`, `comment` nullable, plus the same
tombstone-delete and `version` columns comments/issues already use) -- no remaining-estimate linkage,
since D12 dropped time estimates from V1 entirely, so there is nothing for a worklog to adjust. The
permission model deliberately differs from comments: D13 drops the own-vs-others edit/delete split
entirely, so `TicketService::addWorklog`/`editWorklog`/`deleteWorklog` all require only
project-Member-or-above on the issue's project -- any project member may edit or delete *any* worklog on
an issue they can access, not just the one they logged themselves (unlike D83's author-or-admin rule for
comments). `editWorklog` shares the same `expectedVersion` -> `Domain::ConcurrencyConflict` (409)
optimistic-locking contract as comment/issue edits. New `GET`/`POST /api/v1/issues/{key}/worklogs` and
`PATCH`/`DELETE /api/v1/issues/{key}/worklogs/{id}` routes. `web/` gained a "Time tracking" section in the
issue drawer: a list of logged entries (duration formatted as e.g. "1h 30m", author, date, optional
comment) each with a Delete button shown unconditionally (no client-side author check, since the server
itself allows any project member to delete any entry), and a log-time form accepting a free-text duration
like "1h 30m" or "45m" (parsed client-side, with an HTML5 `pattern` attribute as a first line of defense
and a JS-level parse-and-toast fallback). Browser-verified: logging time shows the correct formatted
duration and comment in the list; deleting an entry removes it; an unparseable duration is rejected before
it reaches the server. Verified against live PostgreSQL directly (`addWorklog`/`listWorklogs`/
`findWorklogById`/`editWorklog`, including the stale-version-conflict rejection, and `deleteWorklog`).

A twelfth batch added the simple append-only admin/security audit log (D23) -- the last item in Phase 4
(Collaboration), which is now fully implemented per `docs/REDUCED_SCOPE_ROADMAP.md`. Migration
`012_audit_log.sql` adds `audit_events` (`id`, `category`, `action`, `actor_user_id` nullable,
`target_type`/`target_id` nullable, `details` nullable, `created_at`) -- no categories-as-a-retention-
feature, export, or configurable retention beyond what's here; rows are simply appended and never updated
or purged. Rather than hooking every write in the codebase, a small, deliberately focused set of
admin/security-relevant actions record an event as a side effect: `AuthService::login` on a wrong
password (`auth`/`login.failed`) or an attempt against an already-locked account (`auth`/`login.blocked`),
`AuthService::createUser` (`identity`/`user.created`, with no actor since `ticket-hub-cli create-user`
runs outside any web session), and `TicketService::setAnonymousReadEnabled`/`permanentlyDeleteProject`/
`permanentlyDeleteIssue` (all `admin`-category). `IDatabase::recordAuditEvent` is fire-and-forget (`void`,
unlike `createNotification`, whose result the notification list feature reads back immediately);
`listAuditEvents(limit)` is newest-first with no pagination or filtering. New
`GET /api/v1/admin/audit-events` route and `TicketService::listAuditEvents`, both global-administrator-only,
the same access level as the recycle bins. `web/` gained a new "Audit log" nav item (hidden for
non-admins, shown and hidden again on logout to avoid leaking it to whoever logs in next in the same
browser tab) rendering a simple read-only table. Browser-verified: the nav item is invisible to a
non-admin and visible to the global admin; toggling the anonymous-read setting on and off produces two
rows in the log showing the correct action and actor. Verified against live PostgreSQL directly,
including confirming that a CLI-driven `create-user` call actually produced an `identity`/`user.created`
event with no actor, end-to-end through the real CLI binary (not just a direct database call).

A thirteenth batch started Phase 5 (Attachments and Kanban board) with ad-hoc issue filter/search
widening (D10/D43). `Domain::IssueFilter` gained `issueTypeKey`/`priorityKey`/`assigneeEmail`/`label`/
`dueBefore`, alongside the pre-existing `projectKey`/`statusKey`/`search` -- still the ad-hoc, in-UI-only
filter model (no saved/shared filters, no JQL, not usable as a webhook/board source). `SqliteDatabase::
listIssues`/`PostgresDatabase::listIssues` both widened to match: type/priority/assignee are equality
joins against already-present query aliases; `dueBefore` is an inclusive `<=`; `label` is a fresh `EXISTS`
subquery against `issue_labels`/`labels` rather than a condition on the already-joined/aggregated
label-list column used to display an issue's labels, so a label filter narrows matches without truncating
a matching issue's own label list; `search` now also matches the issue description, not just summary/
issue key, per D43's plain-substring, no-full-text-index scope. `GET /api/v1/issues` accepts matching new
query parameters; `TicketService::listIssues` needed no change. `web/`'s Issues view filter bar gained
type/priority/assignee dropdowns, a label input, and a due-date picker; the "Clear" button and the
Board-view/global-search transitions all reset the new fields too, so a lingering ad-hoc filter can't leak
into a different view. New SQLite-integration test coverage for every new field individually, a combined
multi-field filter, the inclusive `dueBefore` boundary, and an explicit check that filtering by label
doesn't corrupt the filtered issue's own label list. Verified against live PostgreSQL directly (a
standalone smoke-test program exercising the same cases against `PostgresDatabase::listIssues`) and
browser-verified with Playwright/Chromium (each filter narrows the Issues table correctly, "Clear"
restores the full list, and the Board view doesn't inherit a lingering Issues-view filter), plus a full
regression re-run of the markdown/mentions/reactions/worklog/audit-log/comment-editing browser tests.

A fourteenth batch continued Phase 5 with personal dashboard widgets (D24). `Domain::DashboardStats`
gained `assignedToMe`/`watchedIssues`/`upcomingDeadlines`, matching D24's fixed widget set (assigned
issues, watched issues, recent activity, deadlines, simple stats -- no active-sprint widget, since Scrum
was removed for V1). New `IDatabase::listWatchedIssues(userId, limit)` in both adapters, the reverse
direction of the existing `listWatchers`. `TicketService::dashboard` personalizes for an authenticated
actor -- `assignedToMe` reuses the existing `listIssues` assignee filter and excludes Done-category
issues, `upcomingDeadlines` is derived from that same result set app-side rather than a second database
round trip, `watchedIssues` calls the new method -- and all three stay empty for an anonymous viewer.
`GET /api/v1/dashboard` gained the three new arrays. `web/`'s Dashboard view gained "Assigned to me", "Issues
I'm watching", and "Upcoming deadlines" panels, shown only when a principal is present. New
SQLite-integration coverage for `listWatchedIssues` and authorization-integration coverage for the
dashboard personalization (anonymous gets empty widgets even with anonymous read enabled; Done-category
issues excluded from assigned-to-me; watched-issues reflects a fresh watch). Verified against live
PostgreSQL directly (`listWatchedIssues` across multiple users, overlapping watches, the `limit`
parameter, and cleanup back to empty). Browser-verified with Playwright/Chromium: a real Watch-button
click in the issue drawer populates the watching widget after returning to the dashboard; a real due-date
edit through the API populates the deadlines widget with the correct formatted date; two different users
(alex, sam) each see their own personalized widgets, not each other's. A first draft of the browser script
produced confusing results from state left over by an earlier run that crashed mid-test (a stale watch on
an issue from a script that hit a drawer-backdrop click interception); re-running against a freshly
reseeded server produced clean results, confirming the confusion was test-script state pollution across
runs against the same long-lived dev server, not an application bug.

A fifteenth batch continued Phase 5 with Kanban board WIP limits (D32/D33). New migration
`013_board_columns.sql` adds `board_columns` -- a single flat, installation-wide table (`id`, `status_id`
unique FK to `issue_statuses`, `wip_limit` nullable, `sort_order`) with no `board_id`/`project_id` column
at all, following `docs/REDUCED_SCOPE_DATA_MODEL.md`'s target schema literally and D32's "one board
column equals one workflow status": a WIP limit set on a column applies to that status's column on every
project's board, since there is no per-project board identity in the reduced-scope model.
`002_seed_demo.sql` seeds the five rows ("In Progress" given a demo limit of 3, the rest unlimited). New
`Domain::BoardColumn`; `IDatabase::listBoardColumns()` (ordered by `sort_order`) and
`setBoardColumnWipLimit(statusKey, optional<int>)` (`false` for an unknown status key) in both adapters;
matching `TicketService` methods (read is the same access rule as projects/issues, set is
global-administrator-only like the anonymous-read toggle, an unknown status key throws
`std::invalid_argument`). New `GET /api/v1/board-columns` and `PUT /api/v1/board-columns/{statusKey}` routes.
`web/`'s Board view shows each column's live count as `N / limit` (or plain `N` when unlimited) with a
soft, display-time-only highlight when over limit -- never blocking a status change or issue creation into
that column -- and gives global admins an inline editor to set/clear each column's limit. Drag-and-drop
board reordering was deliberately left out of this batch: neither D32 nor D33 mentions it, and the
roadmap's "board usable end-to-end" exit gate was already satisfied by the pre-existing click-to-drawer
status change. New SQLite-integration and authorization-integration test coverage. Verified against live
PostgreSQL directly (five seeded columns, the demo WIP limit, set/clear/unknown-key cases). Browser-verified
with Playwright/Chromium: a non-admin sees counts only, a global admin sees and can use the inline editor,
pushing a column over its limit shows the highlight and raising the limit clears it, and switching to a
second project while the setting is unchanged confirms it is genuinely installation-wide rather than
scoped to whichever project's board happened to be open when it was changed.

A sixteenth batch completed Phase 5 (and closed out Milestone 2) with the full attachments vertical
(D15/D98-D105). `attachments.sha256`/`deleted_at`/`deleted_by_user_id` already existed from
`003_product_foundation.sql`, pre-provisioned well ahead of this phase; migration `014_attachments.sql`
adds only the `issue_id` index that table never got. New `Domain::Attachment` (`id`, `issueId`,
`issueKey` -- resolved via a join purely for the recycle bin's display, `uploader`, `fileName`,
`contentType`, `byteSize`, `sha256`, `createdAt`, `deletedAt`). `IDatabase::createAttachment` is the one
create* method that takes a caller-supplied `id`: the local filesystem storage key (D15, hardwired, no
storage-backend abstraction) must be known -- and the file already written -- before the row is inserted,
so a row never describes a file that doesn't exist on disk. `listAttachments`/`findAttachmentById`/
`softDeleteAttachment`/`restoreAttachment`/`listDeletedAttachments`/`permanentlyDeleteAttachment` in both
adapters mirror the issue/project/comment tombstone pattern; `listAttachmentStorageKeysForIssue`/
`...ForProject` return every attachment's storage key regardless of soft-delete state, used to delete
files on disk before a permanent issue/project delete cascades through the database (D105 has no
periodic orphan-file audit at all). New `src/infrastructure/storage/LocalAttachmentStorage`: a plain,
non-virtual class (D15 -- no abstract storage port, so no S3-shaped extension point either), keyed by the
attachment's own UUID, rooted at `TICKETHUB_ATTACHMENTS_DIR`. New `Domain::validateAttachmentUpload`
enforces D98's fixed limits (25MB/file, 20/issue, a blocked-extension denylist -- no admin configuration,
no MIME allow-list, no quotas). `TicketService::uploadAttachment` (project-Member-or-above) computes the
SHA-256 at upload time (D105, never re-verified); `downloadAttachment`/`listAttachments` mirror comments'
read-access rule; `deleteAttachment` is uploader-or-project-Admin-or-above (mirroring D83's comment rule,
the closest precedent); `listDeletedAttachments` implements D102's fixed 90-day on-demand retention
itself, one layer above the SQL adapter, since purging an attachment also means deleting its file.
`permanentlyDeleteIssue`/`permanentlyDeleteProject` were both extended to collect and delete affected
attachment files before the database cascade runs. New `GET`/`POST /api/v1/issues/{key}/attachments`,
`DELETE /api/v1/issues/{key}/attachments/{id}`, `GET /api/v1/attachments/{id}/download` (not nested under
`/issues/{key}`, since a download/preview URL only ever needs the id), and the recycle-bin routes (all
global-admin-only). `web/`'s issue drawer gained a sortable Attachments section (D101: name/size/date/
uploader/type), drag-and-drop upload, and native-element previews for all four D99 kinds (`<img>` for
images, `<iframe>` for PDF and text, `<audio>`/`<video>` for the rest). The Markdown toolbar gained full
upload + drag/drop + paste (D100) wherever an issue key is already known (both comment textareas, the
issue-edit description -- deliberately not the create-issue form, since no issue exists yet to attach to),
inserting `![name](attachment://id)`/`[name](attachment://id)` at the cursor;
`renderMarkdownInline` gained real image-syntax support (previously absent entirely) and resolves
`attachment://<id>` to a real download URL, validating the id shape first and leaving anything malformed
as inert text -- the same "safe by construction" posture as the existing `javascript:`-scheme guard. A new
admin-only "Attachment recycle bin" nav item mirrors the audit log's visibility pattern. New
SQLite-integration and authorization-integration test coverage (full CRUD, both permission rules, the
fixed limits, the recycle-bin split). Verified against live PostgreSQL directly. Extensively
browser-verified with Playwright/Chromium: upload/preview/sort/delete through the real UI; all four
preview kinds rendering the correct native element; real native `drop`/`paste` DOM events (not just
`setInputFiles`) working on both the dedicated dropzone and directly on the Markdown editor; an inserted
`attachment://` reference actually resolving to a working download link once a comment is posted and
rendered; and the full recycle-bin restore/permanent-delete flow. Two real bugs were caught and fixed
before this could be considered complete: a Postgres-only "inconsistent types deduced for $1" error from
reusing one placeholder for two differently-typed columns in the `INSERT`, and a redundant first-draft
migration that tried to re-add three columns the schema already had (caught immediately by `ctest`,
never shipped). **This closes out Phase 5 -- every item in `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 5 list
is now implemented, and Milestone 2 is fully closed.**

A seventeenth batch started Phase 6 (Milestone 3) with personal access tokens (D39/D40). New migration
`015_personal_access_tokens.sql` adds `personal_access_tokens`, mirroring `sessions` (same SHA-256
token-hash convention, same "raw value returned once at creation" rule) plus `name`, `last_used_at`, and
`revoked_at`. New `Domain::PersonalAccessToken`/`CreatedPersonalAccessToken`; matching `IDatabase`/
`AuthService` methods (`findPersonalAccessTokenByHash` filters out expired/revoked tokens at the SQL
layer, exactly like session lookup does; `revokePersonalAccessToken` is ownership-scoped). `Api.cpp`'s
`resolvePrincipal` now also accepts an `Authorization: Bearer <token>` header when no session cookie is
present; `csrfTokenValid` now exempts any request with no session cookie in play, since CSRF only
defends against a browser silently attaching a cookie -- this needed zero changes to the ~50 existing
route handlers that already call it. New self-service `GET`/`POST /api/v1/tokens` and
`DELETE /api/v1/tokens/{id}` routes. New identity-integration test coverage. Verified against live
PostgreSQL directly, and end-to-end via `curl` against a running server: created a token via cookie auth,
used it as a Bearer header with no cookies at all to both read `/api/v1/auth/me` and **write** via
`POST /api/v1/projects` with no CSRF header (confirming the exemption through the real HTTP layer), watched
`lastUsedAt` update, revoked it, and confirmed the same token then gets a 401. There is no web UI yet for
managing tokens -- `/api/v1/tokens` is fully functional but reachable only via `curl`/scripts today.

An eighteenth batch continued Phase 6 with the active-session list and "sign out everywhere" endpoint
(D54, resequenced from Phase 1). No new migration -- `sessions` already had everything needed. New
`IDatabase::listSessionsForUser(userId)`/`deleteOtherSessionsForUser(userId, keepSessionId)` in both
adapters; new `AuthService::currentSession(sessionToken)` (resolves the session row itself, not just the
`Principal`), `listActiveSessions`, and `signOutOtherSessions`. New `GET /api/v1/sessions` and
`POST /api/v1/sessions/sign-out-others` routes, deliberately session-cookie-only (not `resolvePrincipal`,
which would also accept a PAT) since "your active web sessions" has no meaning for a PAT-authenticated
caller. "Sign out everywhere" keeps the caller's own current session active and only removes the others
-- a conservative default, since no decision text specifies this, documented explicitly in
`docs/VERIFICATION.md`. New identity-integration test coverage. Verified against live PostgreSQL
directly, and end-to-end via `curl` simulating two browser tabs: listed both sessions with the right one
marked `isCurrent`, signed out the other from tab A, confirmed tab A stayed authenticated while tab B got
a 401, and confirmed a follow-up list showed only the surviving session. There is no web UI yet for
viewing or signing out sessions.

A nineteenth batch continued Phase 6 with fixed rate limits (D124/D125). A new in-memory
`TicketHub::Web::RateLimiter` (`src/web/RateLimiter.h/.cpp`, a thread-safe fixed-window counter with lazy
sweeping of expired entries) implements exactly D124's "simple fixed rate limit per IP/user ... no admin
config, no per-endpoint/service-account exceptions": a 20-attempts-per-IP-per-15-minutes limiter on
`/api/v1/auth/login` (complementing, not replacing, the existing per-account 10-attempts/15-minutes lockout),
and a 120-requests-per-minute limiter (keyed by user id when authenticated, else by IP) shared across
every write route (POST/PUT/PATCH/DELETE), both returning 429 with a `Retry-After` header (900 or 60
respectively) on trip -- the original (pre-simplification) D124 description paired 429 with Retry-After,
and the V1 simplification only dropped the admin-configurable multi-level limits, not that response
contract. The write limiter reuses the same
near-universal chokepoint as CSRF checking -- all 43 existing `csrfTokenValid` call sites -- via a single
scripted text substitution, plus one hand-written overload for the sole route
(`/api/v1/sessions/sign-out-others`) that resolves a `Domain::Session` instead of a `Domain::Principal`.
`RateLimiter` has no Crow dependency, so it is covered by its own standalone test binary
(`ticket-hub-ratelimiter-tests`) that builds and passes even in the SQLite-only and PostgreSQL-only
configurations. No database/migration changes, so no live-PostgreSQL check applied here. Verified
end-to-end via `curl` against a running server: 20 wrong-password login attempts all returned 401, the
21st/22nd returned 429; after a fresh restart, 120 consecutive authenticated `POST /api/v1/projects` requests
returned non-429 codes and the next 10 all returned 429, while a `GET` issued immediately afterward still
returned 200 (confirming only writes are limited). There is no web UI change for this batch -- a 429
response surfaces through the existing generic API-error handling like any other error status.

A twentieth batch continued Phase 6 with the versioned `/api/v1` prefix (D127). Every route in `Api.cpp`
(70 `CROW_ROUTE` registrations) moved from `/api/...` to `/api/v1/...` via a single scripted regex
substitution, except `GET /api/health`, deliberately kept unversioned -- the common infra/monitoring
convention, not specified by any decision text, documented explicitly. `web/app.js`'s ~63 hardcoded API
call sites (there is no single base-URL constant; every call site specifies its own path) were updated
the same way. Purely a URL rename -- no server/domain/database logic changed. Verified via `curl` (the old
unversioned `/api/projects` now 404s; `/api/v1/auth/login` and `/api/v1/projects` work as before) and a
full regression pass with the existing Playwright suite (login/logout, full project lifecycle including
recycle bin and role-gating, issue watch/vote/clone/links/edit, reorder/move/bulk actions), all green
against the renamed routes. Formal `/api/v2` deprecation policy remains deferred until a real v2 is
needed, per D127.

A twenty-first batch continued Phase 6 with read-only CSV export of issues (D48). New
`GET /api/v1/issues/export.csv` shares `Domain::IssueFilter`'s query parameters and authorization with the
existing `GET /api/v1/issues` JSON list route -- an export is always scoped to whatever the caller could
already see via the list view. RFC 4180-style field escaping (quote-wrap on comma/quote/newline, doubled
internal quotes); columns are key/project/summary/description/type/status/priority/reporter/assignee/
storyPoints/dueDate/resolution/labels (semicolon-joined)/createdAt/updatedAt. No CSV import, no Jira
migration tool, per D48's explicit scope. `web/`'s Issues view gained an "Export CSV" link next to the
recycle-bin toggle (hidden in the recycle-bin view, since that has no meaningful export), building its
`href` from the same filter state as the JSON list fetch so the download always matches whatever is
currently filtered/visible. No database/migration changes, so no live-PostgreSQL check applied. Verified
via `curl` (correct `Content-Type`/`Content-Disposition`, correct CSV body, filters apply, 401 when
unauthenticated) and Playwright (clicked the link, captured the actual browser download, confirmed the
suggested filename and row count, then filtered by project and confirmed the re-downloaded CSV contains
only that project's issues).

A twenty-second batch continued Phase 6 with fixed request/batch-size constants (D125). Every JSON
request body (20 call sites) is now capped at 1 MiB, enforced against `request.body.size()` immediately
before parsing, returning `413` if exceeded -- via the same single-scripted-substitution technique used
for the CSRF/rate-limit chokepoints in earlier batches. Every `POST /api/v1/issues/bulk/*` route's
`issueKeys` array is capped at 200 items via the one shared `requiredIssueKeys` helper all four routes
already called, returning `400` if exceeded. Max page size is intentionally not implemented -- it has no
meaning until numbered/offset pagination (D126) exists, and is documented as still open rather than
faked. No admin configuration for either limit, per D125. No database changes. Verified via `curl` (a
~1.05MB body returns 413; 201 bulk `issueKeys` returns 400 with the exact boundary at 200; a normal-sized
request is unaffected) and a Playwright regression pass of the reorder/move/bulk-actions flow (the write
paths most directly touched, since bulk actions now flow through the new limit).

A twenty-third batch closed out Phase 6's security hardening pass, and along the way found and fixed a
real vulnerability: an attachment's `Content-Type` is caller-supplied and unvalidated (D98 has no
upload-time MIME allow-list), so a file uploaded with a spoofed `text/html` Content-Type could execute an
embedded `<script>` same-origin -- either via direct download-URL navigation (`Content-Disposition:
inline`) or via the app's own text/PDF preview, which rendered in an unsandboxed `<iframe>`. This was a
real stored-XSS/CSRF-bypass chain reachable by any project member against any other user (including a
global admin) who previewed the malicious attachment. Fixed in two independent layers: both preview
`<iframe>`s now carry `sandbox=""` (the load-bearing fix, closing the vulnerability regardless of
Content-Disposition), and the download route now serves `Content-Disposition: attachment` for any
content type that could render as an executable document (text/html, xhtml, svg, xml, javascript
variants) via a new `contentTypeSafeToRenderInline` deny-list, leaving normal image/audio/video/PDF/
plain-text previews unaffected (confirmed via `<img>`/`<audio>`/`<video>` not honoring
`Content-Disposition` in the first place). Also added standard security headers
(`X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, `Referrer-Policy: same-origin` on every
response; a `Content-Security-Policy` with `script-src 'self'` on the HTML document), reviewed
dependencies (Crow pinned to release tag `v1.3.3`, already good practice), and reviewed session/CSRF
cookie flags (confirmed no regressions across the session's earlier batches). Note: the roadmap's
referenced `handoff/KNOWN_CONSTRAINTS_AND_RISKS.md` does not exist in this repository; the review was
performed as a direct code audit instead. Verified via `curl` (headers present on the right responses;
a spoofed-`text/html` `.txt` upload now downloads instead of rendering; a real PNG still previews inline)
and Playwright (all four attachment preview kinds still render correctly through the sandboxed iframes; a
targeted XSS-reproduction test confirms the malicious payload's `alert()` no longer fires). With this
batch done, Phase 6's only remaining item was numbered/offset pagination (D126).

A twenty-fourth batch implemented that remaining item: numbered/offset pagination (D126) for
`GET /api/v1/issues`, closing out Phase 6's entire roadmap list. While implementing it, found that the
route's SQL had always silently capped results at 200 rows -- undocumented anywhere, with no way for a
caller to know if a response was truncated. New `Domain::Page<T>` template (`items`/`page`/`pageSize`/
`totalItems`/`totalPages()`) and fixed `DefaultPageSize`/`MaxPageSize` constants (200, matching D125's
"max page size" and the pre-existing cap, so a caller sending neither `page` nor `pageSize` gets exactly
the same result set as before -- purely additive, not a behavior change). New `IDatabase::listIssues(filter,
limit, offset)` and `IDatabase::countIssues(filter)` in both adapters (the existing unpaginated
`listIssues(filter)` overload is untouched, still used internally for the dashboard's "recent"/"assigned
to me" fetches and by the CSV export route, which deliberately wants everything matching the filter, not
one page); new `TicketService::listIssuesPaged` clamps `page`/`pageSize` into range and pairs a count
query with the limited/offset one. `GET /api/v1/issues`'s response envelope gained `page`/`pageSize`/
`totalItems`/`totalPages` fields alongside the existing `items` array. New SQLite-integration test
coverage (limit/offset/pagination-plus-filter composition, an offset past the end returning empty rather
than erroring, a limit exceeding the total returning everything). Verified end-to-end via `curl` against
both a local SQLite server and, for the first time this session, a real local PostgreSQL 16 server
(started via `pg_ctlcluster`/`service postgresql start`, a fresh throwaway database/role, dropped
afterward) -- identical pagination/filtering/count behavior confirmed on both backends, plus confirmed
the CSV export and dashboard routes are unaffected. Also re-ran the full three-configuration build matrix
(full/SQLite-only/PostgreSQL-only) and a Playwright regression pass of the reorder/move/bulk-actions flow
(the demo UI's heaviest consumer of the issues list), all green -- confirming the additive response shape
doesn't break the existing UI. This is a deliberate partial rollout of D126: only `GET /api/v1/issues` is
paginated so far, documented explicitly as still-open for every other list endpoint.

**This closes Phase 6.** A twenty-fifth batch opened Phase 7 with backup and restore (D106-D108). New
`IDatabase::backup(directory)`/`restore(directory)` in both adapters, alongside a new
`ticket-hub-cli backup <output-directory>` and `ticket-hub-cli restore <backup-directory> --yes`.
SQLite uses the SQLite online backup API (`sqlite3_backup_init`/`step`/`finish`) rather than a raw file
copy, since the database runs in WAL mode and a plain `cp` of only the main file could miss data still
sitting in an unmerged `-wal` file; PostgreSQL shells out to `pg_dump --clean --if-exists` for backup
(self-contained for a direct restore into a non-empty target) and `psql -v ON_ERROR_STOP=1` for restore
(aborts on the first SQL error rather than silently reporting success on a partial failure). The CLI
commands handle the attachments-directory copy themselves (not a database concern) and `restore` runs
pending migrations afterward as a visible, separate step (D109: forward-migrate an older backup).
`restore` requires an explicit `--yes` flag, printing a clear warning and refusing to proceed without it
(D108: "a confirmation warning"). Confirmed `ticket-hub-cli migrate` already fully satisfies D111 ("the
existing migrate command suffices") -- no new upgrade command needed. New SQLite-integration test
coverage (`backup` writes a non-empty file; `restore` reverts a database that was mutated after the
backup was taken back to exactly the backup's state). Verified end-to-end exactly matching Phase 7's exit
gate -- "a fresh install → seed → backup → restore cycle is scripted and tested on both databases" -- on
both SQLite and a real live local PostgreSQL 16 server (`pg_ctlcluster`/`service postgresql start`, a
throwaway database, dropped afterward): seeded demo data, added a marker attachment file, backed up,
destroyed the live database and attachments directory entirely (dropped the schema with `CASCADE` for
Postgres; deleted the file for SQLite), confirmed `restore` without `--yes` refuses, then restored with
`--yes` and confirmed the issue count, project keys, and the marker attachment file all round-tripped
correctly on both backends. Also re-ran the full three-configuration build matrix, all green.

A twenty-sixth batch closed out the rest of Phase 7: structured JSON logs to stdout (D133) and the in-app
admin version banner (D112). New `TicketHub::Web::JsonLogHandler` (`src/web/JsonLogHandler.h/.cpp`)
implements Crow's `ILogHandler` interface and is registered once via `crow::logger::setHandler` before
`app.run()`, replacing Crow's default plain-text-to-stderr handler -- every log call Crow already makes
internally (server startup, per-request Info lines, warnings/errors) becomes one JSON object per line
(`timestamp`/`level`/`service`/`message`) on stdout instead, with no new call sites needed anywhere else
in the app; the startup banner line was also switched from a raw `std::cout <<` to `CROW_LOG_INFO` so it
goes through the same handler. For D112, confirmed there is no outbound-HTTP-client infrastructure
anywhere in this codebase and no decision text specifies how the app would discover the latest available
version (no URL, no manifest, no external service) -- adding an actual "check for updates" mechanism
would be new capability, not implementing an existing decision. The conservative, documented choice
instead: a global-administrator-only `installation_settings` key (`latest_known_version`, reusing the
existing generic key/value table -- no new migration) that an admin sets manually (e.g. after checking a
release page themselves); new `GET`/`PUT /api/v1/settings/latest-known-version` (mirroring the existing
anonymous-read-access toggle's route pattern exactly) compares it against the compiled-in
`TICKETHUB_VERSION` and reports `updateAvailable`. `web/`'s app shell gained a dismissible-free banner
element (shown only to admins, only when `updateAvailable` is true) that calls this endpoint once during
`loadBaseData()`. **This closes Phase 7's entire roadmap list.** Verified via `curl` (JSON log lines
parse and contain the expected fields; the settings GET/PUT round-trip correctly, an empty version is
rejected with 400, a non-admin gets 403) and Playwright (an admin with a newer configured version sees
the banner with the correct text; a non-admin does not see it at all), plus a full regression pass of the
project-management flow and the three-configuration build matrix, all green.

A twenty-seventh batch opened Phase 8 (Milestone 4, packaging and release hardening) with the official
Docker image and Docker Compose distribution path (D50). New two-stage `Dockerfile` (full toolchain to
compile, including Crow's network-fetched CMake `FetchContent`; slim `debian:bookworm-slim` runtime image
with just `libpq5`/`libsqlite3-0`/`libargon2-1`/`curl`, a non-root user, and a `HEALTHCHECK` hitting
`/api/health`). `docker-compose.yml` gained a `ticket-hub` app service alongside the existing `postgres`
one (wired together via `depends_on: condition: service_healthy`, a `ticket-hub-attachments` named
volume for `/data`), so `docker compose up` alone now brings up the full instance -- the existing
`docker compose up -d postgres`-only workflow (documented in "Run with PostgreSQL") is untouched. See
"Run with Docker" above for the exact verification-boundary disclosure: `docker build --check`/
`docker compose config` both pass cleanly, and the exact runtime configuration the container sets was
verified directly on the host (against both a fresh SQLite file and a live PostgreSQL database using the
Compose file's exact connection-string shape), but the actual `docker build` layer pull could not run in
this sandboxed environment due to a network-egress policy block on the specific CDN host Docker Hub
redirects to for image layers -- confirmed via the agent proxy's own status log as a policy decision, not
a bug, and not something to route around per this environment's operating rules.

A twenty-eighth batch continued Phase 8 with light and dark theme support (D46). `web/styles.css` now
declares `color-scheme: light dark` and a full `@media (prefers-color-scheme: dark)` override block for
every CSS custom property, so the UI automatically follows the operating system's dark-mode signal -- no
manual in-app toggle or persisted preference, since D46 only calls for "light and dark theme... simple"
and does not ask for one. Roughly 30 previously-hardcoded literal colors across the stylesheet were
converted to reference the variable set instead, so chips, banners, modals, the board, tables, and buttons
all theme consistently; the sidebar, avatar badges, and toast notifications were deliberately left as
fixed colors since they are self-contained and stay readable in either theme. Verification (Playwright/
Chromium) found and fixed one real bug along the way: the issue-linking `.link-form input` field had no
dark-mode styling at all and rendered as a stray white box against the rest of the dark page. Verified by
emulating both `colorScheme: 'light'` and `'dark'` at the browser level -- confirmed light mode is
unchanged, confirmed dark mode's computed styles match the new variables, reviewed screenshots of five
views (Issues list, issue drawer, create-issue modal, Kanban board, Projects grid) for readability, and
re-ran both existing Playwright regression scripts clean to confirm no functional regression from this
CSS-only change.

A twenty-ninth batch closed out D47 (accessibility baseline) and D139 (browser support). Reviewing the
existing UI against D47's reduced V1 bar -- "reasonable baseline accessibility (semantic HTML, keyboard
operability) without... a formal WCAG level or dedicated audit deliverable" -- found that semantic HTML
was already largely in place (landmark elements, labeled form controls, dialog roles, `aria-label`s on
icon buttons), but one real gap existed: issue table rows, Kanban board cards, project cards, and inline
issue-key cross-reference links were only wired up with a plain mouse `click` listener on a non-interactive
element (`<tr>`/`<article>`/`<span>`), with no way for a keyboard-only user to reach or activate them at
all. Fixed with a new shared `makeKeyboardActivatable` helper in `web/app.js` (adds `tabindex="0"`,
`role="link"`, and an `Enter`/`Space` keyboard handler, carefully guarded so it can never double-fire
alongside the already-independently-operable nested controls like the bulk-select checkbox or the reorder
buttons), plus a small CSS rule giving the newly-focusable elements a visible focus ring. D139 (browser
support) needed no code change -- `web/app.js`'s framework-free, unpolyfilled, untranspiled syntax already
satisfies "latest two major versions of Chrome/Firefox/Edge/Safari" by construction, matching the decision
register's own reasoning exactly. Verified with Playwright/Chromium: keyboard-only `Tab`-then-`Enter`/
`Space` activation confirmed for a table row, a board card, and a project card, plus confirmation that the
pre-existing stopPropagation guard on the bulk-select checkbox still holds (no mouse-click regression from
the new keyboard wiring); both existing browser regression scripts re-run clean.

A thirtieth batch closed out the last open Phase 8 item -- and with it, the entire reduced-scope V1
roadmap -- with a dedicated threat-model/security self-review (`docs/THREAT_MODEL.md`), covering every
route in `Api.cpp`, every `TicketService`/`AuthService` authorization check, both database adapters' query
construction, attachment storage, and `web/app.js`'s escaping/CSRF handling. It found and fixed a real
**broken access control (IDOR)** issue: `editComment`/`deleteComment`/`editWorklog`/`deleteWorklog`/
`deleteAttachment` checked the caller's project role against the issue named in the request URL but looked
up the target comment/worklog/attachment purely by its own id, never confirming the resource actually
belonged to that issue -- since every project is readable by every authenticated user (D58), this let a
user with a role on *one* project reach and mutate a comment/worklog/attachment belonging to a *different*
project they had no role on, simply by routing the request through one of their own issues' URLs. Fixed by
requiring the looked-up resource's `issueId` to match the URL-resolved issue before proceeding. Four lower-
severity issues were fixed alongside it: the CSRF cookie was an unnecessary literal prefix of the
`HttpOnly` session token (now generated independently); a login response-time gap let an unauthenticated
caller distinguish "unknown email" from "wrong password" (now equalized with a dummy Argon2id verify);
`/api/v1/auth/logout` was the one mutating route out of 46 missing a CSRF check (added for defense in
depth); and the CSV export was vulnerable to spreadsheet formula injection (fixed with the standard
leading-apostrophe mitigation). Every fix was reproduced live over HTTP before the change and confirmed
fixed after, and new regression tests cover the IDOR fix in `tests/authorization_integration_tests.cpp`.
`docs/THREAT_MODEL.md` also documents what was reviewed and already correct, and three residual risks that
are deliberate consequences of earlier fixed-scope decisions (unscoped PATs, reverse-proxy-unaware rate
limiting, no socket-level body-size cap) rather than new gaps. **This closes Phase 8, Milestone 4, and the
entire reduced-scope V1 roadmap** -- see `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 8 exit gate, now met.

With V1 closed, the user was asked to pick the first piece of optional, non-roadmap follow-up and chose a
**web UI for managing personal access tokens and active sessions** (D39/D40/D54) -- both already had a
complete REST API and CLI-equivalent story since Phase 6; only the `web/` surface was missing, called out
explicitly in every prior completion note. A new always-visible "Account" nav item and view lets any
authenticated user create/list/revoke their own personal access tokens (the raw value is shown exactly
once, per D40, in a dismissible callout with a copy button, then never re-shown) and list/manage their own
active sessions (current session badged, "sign out everywhere else" invalidates every other one while
preserving the caller's). No backend or schema changes -- purely a new consumer of endpoints that already
existed and were already tested. Browser-verified end-to-end with Playwright/Chromium, including a
genuine two-cookie-jar test confirming "sign out everywhere else" actually invalidates the other session
server-side (the second browser context is bounced to the login screen on its next request) while leaving
the caller's own session intact; both existing regression scripts re-run clean.

A second post-V1 batch closed the one gap left open since Phase 3: **re-typing and re-parenting an issue
after creation**. `Domain::EditIssueRequest` gained `issueTypeKey`/`parentIssueKey`;
`TicketService::requireValidHierarchy` was refactored into a shared `validateHierarchyShape` used by both
`createIssue` and `editIssue` -- the same fixed hierarchy rules (D5/D29/D64-D66) apply either way, plus a
new self-parent guard the edit path needs and creation doesn't. Whether a hierarchy-level retype (Epic <->
Story/Task/Bug <-> Sub-task) would leave existing children invalid depends on concurrent database state,
so that one check runs transactionally inside `IDatabase::editIssue` in both adapters -- the same "has
children" precedent `moveIssue` already established -- rejecting a level-crossing retype only when the
issue currently has children; same-level retyping (e.g. Task -> Bug) is always allowed regardless. New
`issue_type`/`parent` `issue_history` rows on change. The issue drawer's edit form gained a Type select and
a Parent/Epic picker mirroring the create modal's. Verified end-to-end over the real HTTP API against
**live PostgreSQL** (both database adapters changed) -- retyped a childless Epic to Task successfully,
confirmed a cross-level retype was rejected for an Epic with a child, re-parented that child to a different
Epic, and confirmed both changes landed correctly in `issue_history` via `psql`. Browser-verified with
Playwright/Chromium, including the server-rejection path (the exact error message surfaces in the edit
form without losing the in-progress edit).

A third post-V1 batch added **drag-and-drop card movement on the Kanban board** -- the board was already
fully usable via the drawer's status dropdown; this adds a faster direct path. Cards are now `draggable`
and each column's card list is a drop target keyed by its status; dropping on a different column applies
the status change through the same `PATCH /api/v1/issues/{key}/status` route the dropdown already uses,
dropping on the same column is a no-op, and dropping on a Done-category column without an existing
resolution opens a small dynamically-built dialog (reusing the existing `.modal-backdrop`/`.modal` styling
rather than introducing new modal CSS) prompting for one first -- the same D68-D70 rule enforced either
way. No backend, schema, or API changes. Browser-verified with Playwright/Chromium; along the way,
Playwright's built-in `dragTo()` helper turned out to be unreliable for longer-distance HTML5 drag
simulation specifically in headless Chromium (short adjacent-column drags worked consistently, longer ones
consistently failed to fire `dragover` on the target), so verification switched to a manual multi-step
mouse simulation, which reliably reproduced every scenario -- direct move, same-column no-op, and the
Done-column resolution prompt (shown, cancelable without side effects, confirmable) -- in both light and
dark mode. Native HTML5 drag-and-drop has no keyboard equivalent; the drawer's status dropdown remains the
keyboard-operable path verified as part of the earlier D47 accessibility baseline pass.

A fourth and final post-V1 batch closed out the two remaining optional items together: the **bulk
Done-status picker** and **keyboard-driven multi-select**. `POST /api/v1/issues/bulk/status` already
accepted and forwarded a shared `resolution` to every issue in the batch (unchanged since the original
bulk-actions implementation); the UI simply excluded Done-category statuses from the bulk picker, so there
was no way to reach it. Fixed by including every status again and adding a resolution picker that appears
exactly when a Done-category status is selected, mirroring the drawer's own reveal-on-selection pattern.
The issues table's checkboxes already supported basic keyboard toggling for free (native
`<input type="checkbox">` semantics), but had no way to select a *range* without the mouse: added a
"select all" checkbox in the table header (synced to a checked/indeterminate/unchecked tri-state), Shift
+click range selection between the last-clicked checkbox and the current one, and Shift+ArrowDown/ArrowUp
on a focused checkbox to extend a range one row at a time with focus moving along -- the genuinely
keyboard-only path the shift-click convention alone doesn't provide. Both changes are entirely
`web/app.js`, no backend/schema/API changes. Browser-verified with Playwright/Chromium in both light and
dark mode, including a direct API read confirming a bulk Done transition applies the identical resolution
to every selected issue, and confirming the pre-existing "checkbox click never opens the drawer" guard
still holds. Both existing browser regression scripts re-run clean. **This was the last item on the
optional, non-roadmap follow-up list identified when the reduced-scope V1 roadmap closed -- there is
currently no further queued work.**

What **was** compiled and tested in this environment, with all warnings enabled
(`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`), for both SQLite and PostgreSQL build configurations:

- `ticket-hub-core` (domain, application, infrastructure/database — including the identity/session code,
  fixed project-role authorization, project lifecycle, the anonymous-read-access toggle, the fixed
  hierarchy/workflow rules, full-replacement issue edit, the fixed issue-link catalog, simple cloning,
  self-service watching/voting, the issue recycle bin, simple bulk actions, manual ordering with
  renumbering, moving an issue between projects, and (Phase 4, now complete) comment editing/tombstone
  delete, fixed emoji reactions, @mention handles/the fixed in-app notification set, simplified worklogs,
  the admin/security audit log, and (Phase 5, now complete) the widened ad-hoc issue filter/search model,
  personal dashboard widgets, Kanban board WIP limits, and the full attachments vertical, in both database
  adapters), plus `Infrastructure::Storage::LocalAttachmentStorage`,
- `ticket-hub-cli` (including `create-user`),
- all eight test binaries (`ctest --output-on-failure`): `domain_validation_tests`, `migration_tests`,
  `sqlite_integration_tests` (`editIssue`: every field, label replacement, assignee clearing, the
  stale-version conflict, one `issue_history` row per changed field; issue links: create, list from both
  ends, duplicate/self-link rejection, find-by-id, delete; watch/vote: idempotency, listing,
  unknown-issue rejection; issue recycle bin: soft-delete/restore/list/permanent-delete lifecycle,
  idempotent no-ops, comment cascade on permanent delete; manual ordering: renumbering on
  reorder-before-anchor and reorder-to-end, cross-project and self-anchor rejection; move: target-project
  rank/counter allocation, alias creation and resolution, `issue_history` write, and rejection of
  same-project moves, unknown-project moves, and moving an issue with a parent or with children; comment
  editing: version increment, `editedAt` set, stale-edit conflict, editing an unknown comment; tombstone
  delete: soft-deleted comments excluded from listing and `findCommentById`, the row and body still
  physically present, deleting an already-deleted comment is a no-op; comment reactions: idempotent
  add/remove per (comment, user, key), multiple users and multiple keys per comment listed correctly;
  `findUserByHandle` resolving the seeded handle and returning nullopt for an unknown one; notifications:
  `createNotification` resolving the issue key via the stored `issue_id`, `listNotifications`/
  `countUnreadNotifications` with the `unreadOnly` filter, `markNotificationRead`/
  `markAllNotificationsRead` idempotency and per-user scoping),
  `identity_integration_tests` (create-user, login success/failure, generic-error anti-enumeration check,
  minimal lockout, session validate/expire/logout, handle normalization/uniqueness/format validation for
  create-user's optional `--handle`), `authorization_integration_tests` (project-role
  gating on issue writes/edits/cloning/links/reorder/move — including the "member of source but not
  target project" move case, not-found semantics under authorization, the anonymous-read-access toggle,
  the full project lifecycle: create/archive/soft-delete/restore/permanently-delete against both
  project-admin and global-administrator paths, confirming watch/vote require no project role unlike
  everything else, the issue recycle bin's project-admin-vs-global-admin split, bulk actions applying
  the same per-issue authorization on a mixed batch of accessible/inaccessible/unknown keys, comment
  edit/delete's simplified author-or-project-admin permissions, including the global-admin-can-moderate-
  any-comment case and unknown-comment nullopt/false returns, comment reactions requiring no project
  role (like watch/vote) while still rejecting an unknown reaction key or an unknown comment id, and the
  fixed notification set -- assigned/self-assigned/unchanged-reassign, mentioned/unknown-handle/
  self-mention, watched-comment/self-watch-self-comment, the mentioned-wins-over-watched dedupe, and
  per-user scoping of mark-read/mark-all-read, each isolated in its own issue with an explicit
  markAllNotificationsRead reset between sub-tests so no sub-test's leftover watcher state can
  contaminate the next one's assertions),
  `workflow_integration_tests` (every Epic/Sub-task hierarchy rejection case, resolution
  required/rejected-if-unknown on completion, resolution cleared on reopen, the sub-task-completion gate,
  reopening leaving a sub-task's status untouched, clone field-copy correctness, the
  sub-task-parent-retention special case, and basic link lifecycle), `crypto_tests`, and
  `ratelimiter_tests` (fixed-window trip/reset, independent per-key buckets) — all passing.
- Additionally, migrations, seed data, `create-user`, and a full login → validate-session → logout cycle
  were manually verified end-to-end against a **live local PostgreSQL 16 server** (not just SQLite) in
  Phase 1; Phase 2 repeated this for the PostgreSQL adapter's authorization/project-lifecycle code
  (`createProject`, `setProjectArchived`, `softDeleteProject`, `listDeletedProjects`, `restoreProject`,
  `permanentlyDeleteProject`, `installation_settings` get/set); Phase 3 repeated it six times, for
  `createIssue`/`changeIssueStatus` (hierarchy, resolution, sub-task gate), `editIssue` (every field,
  label replacement, assignee clearing, stale-version conflict), issue links/cloning (create/list/
  duplicate-and-self-link rejection/find/delete, plus `cloneIssue` including the sub-task special case),
  watch/vote (idempotency, listing, unwatch/unvote, unknown-issue rejection), the issue recycle bin plus
  all four bulk actions (through `TicketService`), and `reorderIssue`/`moveIssue` (renumbering, target
  rank/counter allocation, alias resolution, and the same-project/unknown-project/parent/children
  rejection cases, through `PostgresDatabase` directly); Phase 4 repeated it for `editComment`/
  `deleteComment`/`findCommentById` (version increment, `editedAt`, stale-edit conflict, tombstone
  exclusion from listing, no-op on an already-deleted comment), for `addCommentReaction`/
  `removeCommentReaction`/`listCommentReactions` (idempotent add/remove, multiple users and reaction keys
  per comment listed correctly), and for `findUserByHandle` plus `createNotification`/
  `listNotifications`/`countUnreadNotifications`/`markNotificationRead`/`markAllNotificationsRead`
  (issue-key resolution via the stored `issue_id`, the `unreadOnly` filter, idempotent mark-read/
  mark-all-read, per-user scoping), through `PostgresDatabase` directly — all passing. `ticket-hub-cli
  create-user --handle=<handle>` was also exercised directly against a live PostgreSQL server.

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) are now
built and live-verified as described in "Server verification" above — including every route added across
Phases 1-3: the session-cookie/CSRF wiring, the project-CRUD and anonymous-read-toggle routes, the
`parentIssueKey`/`resolution` request fields, the `PATCH /api/v1/issues/{key}` full-edit route, the HTTP 422
mapping for `Domain::WorkflowViolation`, `POST /api/v1/issues/{key}/clone`,
`GET`/`POST /api/v1/issues/{key}/links`, `POST`/`DELETE /api/v1/issues/{key}/watch`,
`GET /api/v1/issues/{key}/watchers`, `POST`/`DELETE /api/v1/issues/{key}/vote`,
`GET /api/v1/issues/{key}/voters`, `DELETE /api/v1/issue-links/{id}`, `DELETE /api/v1/issues/{key}`,
`GET /api/v1/issues/deleted`, `POST /api/v1/issues/{key}/restore`, `DELETE /api/v1/issues/{key}/permanent`,
`POST /api/v1/issues/bulk/{status,assign,label,delete}`, `POST /api/v1/issues/{key}/reorder`, and
`POST /api/v1/issues/{key}/move`. (Earlier, while adding the edit route, three existing routes --
`POST /api/v1/issues`, `PATCH /api/v1/issues/{key}/status`, `POST /api/v1/issues/{key}/comments` -- were found by
inspection to be missing a `catch (const Domain::Forbidden&)` handler, which would have surfaced a
project-role authorization failure as HTTP 500 instead of 403; fixed before this verification pass, and
the live sweep confirms the fix actually works end-to-end.)

Schema migrations are files such as `001_initial.sql` and `003_product_foundation.sql`. The runner:

1. discovers and sorts schema files,
2. excludes `_seed_` files,
3. calculates a stable content checksum,
4. verifies already-applied checksums,
5. applies each new migration transactionally,
6. records the version and checksum.

After a migration is released, edit it only by adding a new migration. A changed applied migration is rejected.
