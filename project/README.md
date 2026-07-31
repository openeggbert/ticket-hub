# Ticket Hub

Ticket Hub is a self-hosted Jira-like Software issue tracker written in C++20. It uses Crow for HTTP, vanilla HTML/CSS/JavaScript for the web interface, PostgreSQL as the primary production database and SQLite as a smaller single-process backend.

License: MIT.  
Main namespace: `TicketHub`.

## Project status

The current build target is the **reduced-scope V1** (see `REDUCED_SCOPE_SPECIFICATION.md`), not the
original full Jira-like plan in `SPECIFICATION.md`, which remains only as a long-term aspirational
reference. It is **not yet production-ready**: the Kanban board and attachments are future phases
(`docs/REDUCED_SCOPE_ROADMAP.md`).

Implemented now:

- projects and transactional project-local issue keys,
- issue list/detail/create, status changes, labels and comments,
- a simple demo Kanban UI (not yet identity-aware),
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
- every issue/comment/project write now takes an explicit `Principal` instead of a fixed demo user,
- PostgreSQL and SQLite adapters,
- ordered schema migration discovery with stored checksums,
- PostgreSQL migration advisory lock,
- issue optimistic-lock versioning for status updates,
- permanent issue/project key-alias schema foundations,
- recycle-bin schema foundations and live-query filtering,
- domain, migration, crypto, SQLite integration, identity, authorization, and workflow tests (see "Known
  verification limitation" below for what is *not* yet compiled/tested in this environment).

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
TICKETHUB_DB_DRIVER=sqlite ./build-core/ticket-hub-cli create-user "person@example.com" "A Person" "a sufficiently long password" [--admin]
```

`diagnostics` redacts the PostgreSQL connection string. Installed deployments should set `TICKETHUB_MIGRATIONS_ROOT` to the installed migration directory when it differs from the compiled development default.

`create-user` is administrator-only account creation: there is no public registration and no invitation
flow in V1 (`REDUCED_SCOPE_SPECIFICATION.md` section 3). The password is set directly by whoever runs
the command; there is no forced-change-on-first-login flow.

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

## Prototype API

The current unversioned demo API will evolve into `/api/v1` in Phase 6 of `docs/REDUCED_SCOPE_ROADMAP.md`,
alongside PAT authentication, fixed rate limits, and numbered pagination.

| Method | Route | Auth required | Purpose |
|---|---|---|---|
| `GET` | `/api/health` | no | health, version and backend |
| `POST` | `/api/auth/login` | no | `{email,password}` → sets session + CSRF cookies |
| `POST` | `/api/auth/logout` | no | clears session (safe to call unauthenticated) |
| `GET` | `/api/auth/me` | session | current principal, or 401 |
| `GET` | `/api/dashboard` | session, or anon if enabled | counts and recent issues |
| `GET` | `/api/projects` | session, or anon if enabled | active project summaries |
| `POST` | `/api/projects` | session + CSRF, global admin | create project |
| `PATCH` | `/api/projects/{key}/archived` | session + CSRF, project admin | `{archived}` |
| `DELETE` | `/api/projects/{key}` | session + CSRF, project admin | move to recycle bin |
| `GET` | `/api/projects/deleted` | session, global admin | list recycle bin |
| `POST` | `/api/projects/{key}/restore` | session + CSRF, global admin | restore from recycle bin |
| `DELETE` | `/api/projects/{key}/permanent` | session + CSRF, global admin | permanently delete |
| `GET` | `/api/settings/anonymous-read` | session | current toggle value |
| `PUT` | `/api/settings/anonymous-read` | session + CSRF, global admin | `{enabled}` |
| `GET` | `/api/issues` | session, or anon if enabled | filter by `project`, `status`, `q` |
| `POST` | `/api/issues` | session + CSRF, project member | create issue (`assigneeEmail`, `parentIssueKey`) |
| `GET` | `/api/issues/{key}` | session, or anon if enabled | current key or permanent alias |
| `PATCH` | `/api/issues/{key}/status` | session + CSRF, project member | `{statusKey, resolution?, expectedVersion?}` |
| `GET` | `/api/issues/{key}/comments` | session, or anon if enabled | live comments |
| `POST` | `/api/issues/{key}/comments` | session + CSRF, project member | add comment |

Issue responses include `version` and `resolution`. A stale `expectedVersion` returns HTTP 409. A
missing/insufficient project role or global-admin requirement returns HTTP 403. An anonymous read while
the toggle is off returns HTTP 401. A request that violates the fixed workflow's hardcoded rules --
completing an issue without a `resolution`, an unrecognized `resolution`, or completing an issue that
still has an unfinished sub-task -- returns HTTP 422. `parentIssueKey` on issue creation is validated
against the fixed Epic/Sub-task hierarchy (a Sub-task requires a same-project Story/Task/Bug parent, an
Epic may not have one, a Story/Task/Bug's optional parent must be a same-project Epic); a violation
returns HTTP 400, same as any other invalid request field.

Session-authenticated writes require the `X-CSRF-Token` header to match the readable `th_csrf` cookie
set at login (double-submit pattern) — see `src/web/Api.cpp`. **This file has not been compiled in any
sandbox yet** (see "Known verification limitation" below); build and smoke-test it against a real Crow
checkout before relying on it.

There is no login page in the web UI yet — `/api/auth/login` exists but nothing in `web/` calls it. The
demo UI still browses/creates issues without authenticating, which will start failing once the
write routes actually enforce the session check end-to-end in a real Crow build.

## Known verification limitation

The `ticket-hub` server target (Crow-based) could not be built in the authoring sandbox for this
milestone because outbound access to `github.com` — needed to fetch Crow via CMake `FetchContent` — was
blocked by the sandbox's network egress policy. This is the same limitation recorded for the original
prototype in `handoff/IMPLEMENTATION_STATE.md`.

What **was** compiled and tested in this environment, with all warnings enabled
(`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`), for both SQLite and PostgreSQL build configurations:

- `ticket-hub-core` (domain, application, infrastructure/database — including the identity/session code,
  fixed project-role authorization, project lifecycle, the anonymous-read-access toggle, and the fixed
  hierarchy/workflow rules, in both database adapters),
- `ticket-hub-cli` (including `create-user`),
- all seven test binaries (`ctest --output-on-failure`): `domain_validation_tests`, `migration_tests`,
  `sqlite_integration_tests`, `identity_integration_tests` (create-user, login success/failure,
  generic-error anti-enumeration check, minimal lockout, session validate/expire/logout),
  `authorization_integration_tests` (project-role gating on issue writes, not-found semantics under
  authorization, the anonymous-read-access toggle, and the full project lifecycle: create/archive/
  soft-delete/restore/permanently-delete, each checked against both the project-admin and
  global-administrator authorization paths), `workflow_integration_tests` (new — every Epic/Sub-task
  hierarchy rejection case, resolution required/rejected-if-unknown on completion, resolution cleared on
  reopen, the sub-task-completion gate blocking and then permitting a parent's completion, and reopening
  a parent leaving its sub-task's status untouched), and `crypto_tests` — all passing.
- Additionally, migrations, seed data, `create-user`, and a full login → validate-session → logout cycle
  were manually verified end-to-end against a **live local PostgreSQL 16 server** (not just SQLite) in
  Phase 1; Phase 2 repeated this for the PostgreSQL adapter's authorization/project-lifecycle code
  (`createProject`, `setProjectArchived`, `softDeleteProject`, `listDeletedProjects`, `restoreProject`,
  `permanentlyDeleteProject`, `installation_settings` get/set); Phase 3 repeated it again for
  `createIssue` (with `parentIssueKey`) and `changeIssueStatus` (with `resolution`, the sub-task gate,
  and the reopen-clears-resolution rule) — all passing.

What was **not** compiled or tested: `src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the
`ticket-hub` server target). The session-cookie/CSRF wiring in `Api.cpp`, the Phase 2 project-CRUD and
anonymous-read-toggle routes, and the Phase 3 `parentIssueKey`/`resolution` request fields and the new
HTTP 422 mapping for `Domain::WorkflowViolation`, follow the exact patterns already used by the
surrounding (previously-verified) route handlers, but none of it has been built or exercised against a
real HTTP client. Build and smoke-test the server target in an environment with network access to
`github.com` (or a preinstalled Crow package) before trusting it in production.

Schema migrations are files such as `001_initial.sql` and `003_product_foundation.sql`. The runner:

1. discovers and sorts schema files,
2. excludes `_seed_` files,
3. calculates a stable content checksum,
4. verifies already-applied checksums,
5. applies each new migration transactionally,
6. records the version and checksum.

After a migration is released, edit it only by adding a new migration. A changed applied migration is rejected.
