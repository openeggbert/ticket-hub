# Ticket Hub

Ticket Hub is a self-hosted Jira-like Software issue tracker written in C++20. It uses Crow for HTTP, vanilla HTML/CSS/JavaScript for the web interface, PostgreSQL as the primary production database and SQLite as a smaller single-process backend.

License: MIT.  
Main namespace: `TicketHub`.

## Project status

The repository contains a functional vertical-slice prototype and the approved specification for the complete product. It is **not yet production-ready**: authentication, permission enforcement and the full workflow engine are future phases.

Implemented now:

- projects and transactional project-local issue keys,
- issue list/detail/create, status changes, labels and comments,
- a simple demo Kanban UI,
- PostgreSQL and SQLite adapters,
- ordered schema migration discovery with stored checksums,
- PostgreSQL migration advisory lock,
- issue optimistic-lock versioning for status updates,
- permanent issue/project key-alias schema foundations,
- recycle-bin schema foundations and live-query filtering,
- domain, migration and SQLite integration tests.

Authoritative documents:

- [SPECIFICATION.md](SPECIFICATION.md) — approved product behavior and non-goals,
- [docs/PRODUCT_DECISIONS_COMPLETE.md](docs/PRODUCT_DECISIONS_COMPLETE.md) — all 142 chronological product decisions,
- [docs/HANDOFF_NOTES.md](docs/HANDOFF_NOTES.md) — continuation context for the next coding agent,
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — target module and runtime architecture,
- [docs/DATA_MODEL.md](docs/DATA_MODEL.md) — target logical tables and columns,
- [docs/SCHEMA.md](docs/SCHEMA.md) — schema currently implemented,
- [docs/ROADMAP.md](docs/ROADMAP.md) — phased implementation plan.

## Architecture today

```text
Browser (semantic HTML + vanilla JS enhancement)
        |
        v
Crow HTTP/JSON routes
        |
        v
TicketHub::Application::TicketService
        |
        v
TicketHub::Infrastructure::Database::IDatabase
        |                         |
        v                         v
PostgresDatabase (libpq)    SqliteDatabase (sqlite3)
```

The target remains a modular monolith with explicit ports for database, search, attachment storage, mail, jobs, events, cache, secrets and observability.

## Requirements

- CMake 3.25+
- C++20 compiler
- SQLite development files when `TICKETHUB_WITH_SQLITE=ON`
- PostgreSQL client development files when `TICKETHUB_WITH_POSTGRES=ON`
- Crow 1.3.3 for the server target

Typical Debian dependencies:

```bash
sudo apt install build-essential cmake libpq-dev libsqlite3-dev libasio-dev
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
```

`diagnostics` redacts the PostgreSQL connection string. Installed deployments should set `TICKETHUB_MIGRATIONS_ROOT` to the installed migration directory when it differs from the compiled development default.

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

The current unversioned demo API will evolve into `/api/v1` before production compatibility is promised.

| Method | Route | Purpose |
|---|---|---|
| `GET` | `/api/health` | health, version and backend |
| `GET` | `/api/dashboard` | counts and recent issues |
| `GET` | `/api/projects` | active project summaries |
| `GET` | `/api/issues` | filter by `project`, `status`, `q` |
| `POST` | `/api/issues` | create issue |
| `GET` | `/api/issues/{key}` | current key or permanent alias |
| `PATCH` | `/api/issues/{key}/status` | status update; accepts `expectedVersion` |
| `GET` | `/api/issues/{key}/comments` | live comments |
| `POST` | `/api/issues/{key}/comments` | add comment |

Issue responses include `version`. A stale `expectedVersion` returns HTTP 409.

## Migration policy

Schema migrations are files such as `001_initial.sql` and `003_product_foundation.sql`. The runner:

1. discovers and sorts schema files,
2. excludes `_seed_` files,
3. calculates a stable content checksum,
4. verifies already-applied checksums,
5. applies each new migration transactionally,
6. records the version and checksum.

After a migration is released, edit it only by adding a new migration. A changed applied migration is rejected.
