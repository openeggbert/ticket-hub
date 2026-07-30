# CLAUDE.md — Ticket Hub continuation contract

This repository is a handoff from a completed product-definition conversation. Work autonomously from the archived decisions and keep the project buildable and tested.

## Mandatory reading order

Before changing code, read:

1. `README.md`
2. `SPECIFICATION.md`
3. `docs/PRODUCT_DECISIONS_COMPLETE.md`
4. `docs/HANDOFF_NOTES.md`
5. `NEXT.md`
6. `docs/ARCHITECTURE.md`
7. `docs/DATA_MODEL.md`
8. `docs/SCHEMA.md`
9. `docs/ROADMAP.md`
10. `docs/VERIFICATION.md`

Then inspect CMake, migrations, domain/application/database interfaces, tests, and the web layer. Build and run tests before editing.

## Non-negotiable product identity

- Product: Ticket Hub.
- Main namespace: `TicketHub`.
- Language: C++20.
- HTTP server: Crow.
- Frontend: hybrid semantic HTML plus vanilla CSS/JavaScript progressive enhancement; no React, Vue, Angular, or other frontend framework.
- Primary production database: PostgreSQL.
- Optional database: SQLite with the same user-facing features and documented single-instance limits.
- License: MIT.
- First product type: company-managed Software projects only.
- Single-tenant self-hosted installation.

## Source-of-truth hierarchy

1. Later explicit entries in `docs/PRODUCT_DECISIONS_COMPLETE.md` refine earlier choices.
2. `SPECIFICATION.md` is the normative thematic product baseline.
3. `docs/DATA_MODEL.md` is the target logical schema, not proof of implementation.
4. `docs/SCHEMA.md` and tests describe what exists now.
5. `NEXT.md` defines immediate work.

Never claim a target feature is implemented merely because it appears in the specification.

## Current objective

Continue Phase 1: identity and authentication foundation.

- Introduce an explicit `Principal`/actor context.
- Remove fixed demo-user assumptions from write signatures and routes.
- Add migration-safe identity tables and repository/domain contracts.
- Implement the smallest complete local-session authentication vertical slice.
- Prepare groups, invitations, OIDC identities/providers, sessions, PATs, and service-account foundations without attempting all of them in one uncontrolled change.
- Define centralized authorization contracts before adding broad project administration.

## Architecture rules

- Keep a modular monolith with domain/application ports and infrastructure adapters.
- Do not expose SQL or database-specific types to domain/application modules.
- PostgreSQL and SQLite may use different SQL/locking internals but must satisfy one application contract.
- Do not create a generic all-purpose SQL wrapper as the domain abstraction. Prefer capability/repository/service interfaces that express product operations.
- Centralize authentication and authorization rather than duplicating checks in Crow routes.
- Durable side effects must eventually use jobs/outbox/events. Do not use detached in-memory tasks for email, webhooks, indexing, or other restart-sensitive work.
- Use immutable structured history for report/audit-relevant facts.

## Database and migrations

- Released/applied migrations are immutable. Never edit them; add a new ordered migration.
- Keep PostgreSQL and SQLite migrations logically equivalent.
- Preserve UUID identity and stable permanent key aliases.
- Never reuse issue numbers.
- Keep soft-delete/recycle-bin semantics explicit.
- Test migration ordering, idempotence, checksum validation, and upgrades from the previous schema.
- PostgreSQL-specific live integration tests may be conditional on an explicit test connection string, but adapter compilation is always required when enabled.

## Build and test discipline

- Reuse existing persistent build directories when suitable; prefer incremental builds.
- Do not delete or recreate build directories without a concrete reason.
- Keep parallel compilation reasonable for the host.
- Maintain all supported configurations:
  - SQLite + PostgreSQL core,
  - SQLite-only,
  - PostgreSQL-only,
  - server target when Crow is available.
- Add tests for every behavior change.
- Run `ctest --output-on-failure` before finishing.
- When Crow/network dependencies are unavailable, still compile and test the core/CLI/adapters and state the HTTP verification limitation precisely.

## Security rules

- Never log plaintext passwords, session tokens, PATs, client secrets, database credentials, webhook secrets, or encryption keys.
- Store token/session verifiers as hashes where appropriate; reveal secret token material only once at creation.
- Use Argon2id for local passwords when implemented.
- Protect browser sessions with HttpOnly, Secure, and SameSite cookies, CSRF protection, rotation, expiry, and revocation.
- Validate OIDC issuer, audience, signature, nonce, state, redirect URIs, and account-linking behavior.
- Sanitize Markdown-rendered HTML and validate attachment authorization on every access.

## Documentation to update with each milestone

Update as applicable:

- `README.md`
- `NEXT.md`
- `CHANGELOG.md`
- `docs/SCHEMA.md`
- `docs/SCOPE.md`
- `docs/VERIFICATION.md`
- `docs/ARCHITECTURE.md` and `docs/DATA_MODEL.md` if the approved architecture is refined

Record a new ADR only for a material implementation choice not already settled by the decision register.

## Autonomy and questions

Do not reopen settled product choices for minor implementation details. Choose conservative, secure defaults consistent with the archived decisions and document them. Ask the user only when a genuine contradiction, destructive operation, missing credential/service, or unavoidable product tradeoff blocks safe progress.

## Completion report

At the end of a work batch, report:

- exact features implemented,
- migrations added,
- tests and build configurations run,
- known limitations/failures,
- documents updated,
- immediate next work.
