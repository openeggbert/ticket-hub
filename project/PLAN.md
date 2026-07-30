# Ticket Hub plan

The detailed phase plan is maintained in [docs/ROADMAP.md](docs/ROADMAP.md).

## Current milestone: Phase 0 baseline

Completed in this batch:

- [x] Freeze the approved product specification.
- [x] Document architecture and target logical table catalog.
- [x] Replace single-file migration execution with ordered discovery.
- [x] Store and validate migration content checksums.
- [x] Add PostgreSQL advisory migration locking.
- [x] Keep demo seed separate from schema migrations.
- [x] Add issue versioning and optimistic status-change checks.
- [x] Add permanent project/issue key alias tables.
- [x] Add recycle-bin foundation columns and live-query filtering.
- [x] Normalize project/issue key input and labels.
- [x] Fix duplicate SQLite project results.
- [x] Expand domain, migration and SQLite integration tests.

Next work:

- [ ] Split configuration validation from environment loading.
- [x] Add a CLI target with `migrate`, `seed-demo`, version and diagnostics commands.
- [ ] Extend the CLI with `upgrade check`, backup and restore commands.
- [ ] Add PostgreSQL integration tests using an explicit test connection string.
- [ ] Introduce a `Principal` and remove the fixed demo user from application write signatures.
- [ ] Implement identity tables and local session authentication.
- [ ] Add permission-scheme domain contracts before expanding project administration.

## Implementation rules

- Preserve buildability and tests after every change.
- Keep PostgreSQL and SQLite behavior aligned at the application-contract level.
- Never introduce generic SQL into application/domain modules.
- Schema migration files become immutable once included in a release.
- Side effects must eventually use durable jobs/outbox events, not detached in-memory work.
- Do not implement features excluded by `SPECIFICATION.md` without an explicit scope revision.
