# Ticket Hub next work

Current version: 0.2.0  
Current roadmap phase: Phase 0 baseline, then Phase 1 identity.

## Completed in the latest batch

- Approved product specification, architecture, target data model and roadmap.
- Ordered checksummed migrations with PostgreSQL advisory locking.
- Separate demo seed.
- Issue optimistic-lock version and HTTP conflict foundation.
- Project/issue key alias and recycle-bin schema foundations.
- Key/label normalization and SQLite duplicate-project fix.
- Administration CLI for version, diagnostics, migrate and demo seed.
- Domain, migration and SQLite integration tests.
- SQLite-only and PostgreSQL-only build verification.

## Immediate next implementation slice

1. Introduce a `Principal` model and pass it into application write use cases.
2. Add Phase 1 identity migrations:
   - UUID identity with unique email and optional handle,
   - local credentials,
   - sessions,
   - invitations,
   - groups and group memberships,
   - OIDC provider and external identity records.
3. Add local-password hashing behind an authentication port.
4. Add login/logout/session endpoints and CSRF protection.
5. Remove the fixed `demo` user from production write paths while retaining an explicit demo mode.
6. Add authentication and session integration tests for SQLite; add PostgreSQL test wiring through an environment-provided connection string.

## Known verification limitation

The Crow server target could not be configured in the sandbox because the environment could not resolve GitHub and no installed Crow package was available. Core, CLI, SQLite-only and PostgreSQL-only targets compile successfully.
