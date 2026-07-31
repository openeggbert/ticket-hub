# CLAUDE.md — Ticket Hub continuation contract

This repository is a handoff from a completed product-definition conversation. Work autonomously from the archived decisions and keep the project buildable and tested.

> **2026-07-31 scope-reduction amendment:** the product owner walked all 142 decisions again and
> produced a smaller, finishable V1 target. The `REDUCED_SCOPE_*` documents below now take precedence
> over the original full-scope documents of the same name for what to actually build. The originals are
> kept as the long-term aspirational baseline. See `docs/REMOVED_AND_DEFERRED_FEATURES.md` before
> building anything that sounds like OIDC, a workflow engine, custom fields, sprints, email, webhooks,
> or background jobs — it is very likely on that list.

## Mandatory reading order

Before changing code, read:

1. `README.md`
2. `REDUCED_SCOPE_SPECIFICATION.md` (current V1 target; `SPECIFICATION.md` is long-term reference only)
3. `docs/REDUCED_SCOPE_DECISIONS.md` and `docs/REMOVED_AND_DEFERRED_FEATURES.md`
4. `docs/HANDOFF_NOTES.md`
5. `NEXT.md`
6. `docs/REDUCED_SCOPE_ARCHITECTURE.md` (`docs/ARCHITECTURE.md` is long-term reference only)
7. `docs/REDUCED_SCOPE_DATA_MODEL.md` (`docs/DATA_MODEL.md` is long-term reference only)
8. `docs/SCHEMA.md`
9. `docs/REDUCED_SCOPE_ROADMAP.md` (`docs/ROADMAP.md` is long-term reference only)
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

1. `docs/REDUCED_SCOPE_DECISIONS.md` is the current authoritative V1 decision register (supersedes
   `docs/PRODUCT_DECISIONS_COMPLETE.md`, which remains the long-term aspirational ledger).
2. `REDUCED_SCOPE_SPECIFICATION.md` is the current normative V1 product baseline (supersedes
   `SPECIFICATION.md`).
3. `docs/REDUCED_SCOPE_DATA_MODEL.md` is the target V1 logical schema, not proof of implementation
   (supersedes `docs/DATA_MODEL.md`).
4. `docs/SCHEMA.md` and tests describe what exists now.
5. `NEXT.md` defines immediate work.
6. `docs/REMOVED_AND_DEFERRED_FEATURES.md` is authoritative for what NOT to build.

Never claim a target feature is implemented merely because it appears in a specification. Never build a
feature from `docs/REMOVED_AND_DEFERRED_FEATURES.md` without a new, explicit product conversation.

## Current objective

Continue Phase 1 of `docs/REDUCED_SCOPE_ROADMAP.md`: identity and sessions (reduced scope).

- Introduce an explicit `Principal`/actor context.
- Remove fixed demo-user assumptions from write signatures and routes.
- Add migration-safe identity tables (`users`, `local_credentials`, `sessions`) and repository/domain
  contracts — **no** `groups`, `invitations`, `oidc_providers`, or `external_identities`; those are
  permanently removed for V1 (`docs/REMOVED_AND_DEFERRED_FEATURES.md`).
- Implement the smallest complete local-session authentication vertical slice, including an
  administrator-only "create user with temporary password" action (this is the entire
  registration/reset story for V1).
- Define the fixed project-role authorization check (Admin/Member/Viewer) before adding broad project
  administration — this replaces the original configurable permission-scheme plan.

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
