# Current implementation state

> **Stale — historical snapshot only, updated 2026-08-05 to add this notice.** This document captured
> implementation state as of the original 2026-07-30 handoff, before any implementation batch in this
> repository's git history had landed. Every item below under "Not implemented yet" — auth/sessions,
> permissions, custom fields, notifications, webhooks, backup/restore, Docker packaging, and more — has
> since been implemented. The entire reduced-scope V1 roadmap (`project/docs/REDUCED_SCOPE_ROADMAP.md`,
> Phases 1-8) is complete, plus 15 further post-V1 batches (most recently REST write idempotency keys,
> D128). Do not use this file for current status. For current status, see `project/PLAN.md`'s "Current
> status" line, `project/NEXT.md`'s "The roadmap is now complete" section, and `project/CHANGELOG.md`.
> The content below is preserved as a historical record of the state at handoff time only.

Version: 0.2.0 (as of the original 2026-07-30 handoff — see notice above for current version/status)

## Implemented and verified

- C++20/CMake modular-monolith foundation under namespace `TicketHub`.
- Crow route layer source and hybrid vanilla HTML/CSS/JS demo UI.
- Application service layer and database abstraction.
- PostgreSQL adapter using libpq.
- SQLite adapter using sqlite3.
- Ordered migration discovery and stable content checksums.
- Rejection of modified already-applied migrations.
- PostgreSQL advisory migration lock.
- Separate idempotent demo seed.
- Administration CLI with `version`, `diagnostics`, `migrate`, and `seed-demo`.
- Projects and transactionally allocated project-local issue numbers.
- Fixed prototype issue types/statuses/priorities.
- Issue list/detail/create, labels, comments, dashboard, and status changes.
- Issue `version` field and stale status-update conflict detection.
- Project and issue key-alias schema foundations; issue aliases resolve in detail/comment operations.
- Recycle-bin foundation fields and exclusion of deleted issues/comments from normal reads.
- Project/issue key normalization and 2–12 character project-key validation.
- Label normalization and deduplication.
- Domain, migration, and SQLite integration tests.
- SQLite-only and PostgreSQL-only build configurations.

## Verification result from the creation environment

- 3/3 test suites passed.
- Compiler: GCC 14.2.0.
- SQLite: 3.46.1.
- libpq: 17.9.
- PostgreSQL adapter compiled and linked, but no live PostgreSQL server integration test was available.
- The Crow server target could not be compiled in that sandbox because GitHub DNS/network access was unavailable and Crow was not preinstalled. This is an environment limitation, not proof that the HTTP target is correct.

See `project/docs/VERIFICATION.md` for exact commands and coverage.

## Not implemented yet

- Real principals on writes; current write flow still uses a fixed demo identity.
- Local-password account storage and Argon2id.
- Registration modes and invitations.
- OIDC providers and provisioning.
- Browser sessions, PATs, service-account tokens.
- Groups, project roles, permission schemes, and enforcement.
- Published/draft workflow engine, conditions, validators, post-functions, and transition forms.
- Custom fields and contexts.
- Saved filters and persistent Scrum/Kanban boards.
- Sprint/backlog/rank/swimlane/quick-filter behavior.
- Worklogs, versions/releases, components, agile reports.
- Attachment filesystem/S3 storage and APIs.
- Notifications, inbound/outbound mail, jobs, event bus, SSE, webhooks.
- Import/export, Jira migration, backup/restore, upgrade workflow.
- Kubernetes packaging, observability exporters, secrets backends, optional encryption.

## Immediate next milestone

Phase 1 identity and authentication foundation:

1. Introduce `Principal`/actor context and remove fixed demo user IDs from application write signatures.
2. Add identity migrations and repositories for users, credentials, sessions, groups, memberships, invitations, OIDC providers/identities, and token metadata.
3. Implement secure local session authentication.
4. Implement registration-mode enforcement and invitation lifecycle.
5. Add authentication middleware to Crow routes.
6. Preserve PostgreSQL/SQLite parity and extend integration tests.
7. Define permission-scheme domain contracts before broad project administration work.

Do not jump directly to the complete Jira feature set. Advance in tested vertical slices according to `project/docs/ROADMAP.md`.
