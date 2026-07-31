# Ticket Hub

Ticket Hub is a self-hosted Jira-like Software issue tracker written in C++20. It uses Crow for HTTP, vanilla HTML/CSS/JavaScript for the web interface, PostgreSQL as the primary production database and SQLite as a smaller single-process backend.

License: MIT.  
Main namespace: `TicketHub`.

## Project status

The current build target is the **reduced-scope V1** (see `REDUCED_SCOPE_SPECIFICATION.md`), not the
original full Jira-like plan in `SPECIFICATION.md`, which remains only as a long-term aspirational
reference. It is **not yet production-ready**: role-based authorization, the Kanban board, attachments,
and the REST API surface are future phases (`docs/REDUCED_SCOPE_ROADMAP.md`).

Implemented now:

- projects and transactional project-local issue keys,
- issue list/detail/create, status changes, labels and comments,
- a simple demo Kanban UI (not yet identity-aware),
- **local accounts: Argon2id password hashing, server-side sessions, minimal login-attempt lockout**
  (`AuthService` — Phase 1 of `docs/REDUCED_SCOPE_ROADMAP.md`),
- **administrator-only account creation via `ticket-hub-cli create-user`** — there is no
  self-registration or invitation flow in V1,
- every issue/comment write now takes an explicit `Principal` instead of a fixed demo user,
- PostgreSQL and SQLite adapters,
- ordered schema migration discovery with stored checksums,
- PostgreSQL migration advisory lock,
- issue optimistic-lock versioning for status updates,
- permanent issue/project key-alias schema foundations,
- recycle-bin schema foundations and live-query filtering,
- domain, migration, crypto, and SQLite integration tests (see "Known verification limitation" below
  for what is *not* yet compiled/tested in this environment).

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
| `GET` | `/api/dashboard` | no | counts and recent issues |
| `GET` | `/api/projects` | no | active project summaries |
| `GET` | `/api/issues` | no | filter by `project`, `status`, `q` |
| `POST` | `/api/issues` | session + CSRF | create issue (`assigneeEmail`, not username) |
| `GET` | `/api/issues/{key}` | no | current key or permanent alias |
| `PATCH` | `/api/issues/{key}/status` | session + CSRF | status update; accepts `expectedVersion` |
| `GET` | `/api/issues/{key}/comments` | no | live comments |
| `POST` | `/api/issues/{key}/comments` | session + CSRF | add comment |

Issue responses include `version`. A stale `expectedVersion` returns HTTP 409.

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

- `ticket-hub-core` (domain, application, infrastructure/database — including the new identity/session
  code and both database adapters),
- `ticket-hub-cli` (including the new `create-user` command),
- all five test binaries (`ctest --output-on-failure`), including new `crypto_tests` (SHA-256 known-answer
  vectors, Argon2id round-trip) and `identity_integration_tests` (create-user, login success/failure,
  generic-error anti-enumeration check, minimal lockout, session validate/expire/logout) — all passing.
- Additionally, migrations, seed data, `create-user`, and a full login → validate-session → logout cycle
  were manually verified end-to-end against a **live local PostgreSQL 16 server** (not just SQLite),
  confirming the PostgreSQL adapter's identity code path independent of the SQLite one.

What was **not** compiled or tested: `src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the
`ticket-hub` server target). The session-cookie/CSRF wiring in `Api.cpp` follows the exact patterns
already used by the surrounding (previously-verified) route handlers, but it has not been built or
exercised against a real HTTP client. Build and smoke-test the server target in an environment with
network access to `github.com` (or a preinstalled Crow package) before trusting it in production.

Schema migrations are files such as `001_initial.sql` and `003_product_foundation.sql`. The runner:

1. discovers and sorts schema files,
2. excludes `_seed_` files,
3. calculates a stable content checksum,
4. verifies already-applied checksums,
5. applies each new migration transactionally,
6. records the version and checksum.

After a migration is released, edit it only by adding a new migration. A changed applied migration is rejected.
