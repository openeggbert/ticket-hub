# Scope status

The current build target is the **reduced-scope V1**:
[../REDUCED_SCOPE_SPECIFICATION.md](../REDUCED_SCOPE_SPECIFICATION.md), roadmap in
[REDUCED_SCOPE_ROADMAP.md](REDUCED_SCOPE_ROADMAP.md), and table catalog in
[REDUCED_SCOPE_DATA_MODEL.md](REDUCED_SCOPE_DATA_MODEL.md). The original full-scope
[../SPECIFICATION.md](../SPECIFICATION.md), [ROADMAP.md](ROADMAP.md), and [DATA_MODEL.md](DATA_MODEL.md)
remain as the long-term aspirational baseline only — do not build against them directly.

## Implemented prototype slice

- C++20/CMake project and `TicketHub` namespace.
- Crow route layer and hybrid vanilla HTML/CSS/JS demo UI.
- PostgreSQL and SQLite database adapters behind one application-facing interface.
- Ordered checksummed schema migrations and idempotent demo seed.
- Projects and transactional project-local issue numbering.
- Fixed issue types/statuses/priorities used by the prototype.
- Issue creation/list/detail, status changes, labels and comments.
- Issue version exposed for optimistic status-change conflict detection.
- Permanent issue-key alias lookup foundation.
- Recycle-bin columns and live-query filtering foundation.
- Domain, migration and SQLite integration tests.

## Not yet built (still V1 scope — see `REDUCED_SCOPE_ROADMAP.md`)

- Writes still use a fixed demo identity; no real principal, password, session, or PAT implementation
  yet (Phase 1).
- No role enforcement yet (Phase 2).
- Status changes are not yet restricted by the fixed workflow rules (Phase 3).
- Comments/mentions/reactions, attachments, and the Kanban board are not implemented (Phase 4-5).
- The `/api/v1` REST surface, CSV export, backup/restore, and upgrade command are not implemented
  (Phase 6-7).
- Docker packaging and the hardening/accessibility passes are not done (Phase 8).
- `PostgreSQL` runtime integration tests require an external test server and are not included in this
  sandbox verification.

## Permanently out of V1 scope (do not build these)

OIDC, invitations, public registration, configurable permission/notification schemes, the configurable
workflow engine, custom fields, saved/shared filters, Scrum/sprints/agile reports, versions/releases,
issue templates, automation rules, webhooks, service accounts, Git integration, the Jira migration
tool, CSV import, outbound/inbound email, the background job queue, internal event bus, realtime
(SSE), in-memory cache, S3 attachment storage, Kubernetes/Helm/`.deb`/`.rpm` packaging, the i18n
framework, and pluggable secrets/observability backends. Full list with decision numbers:
[REMOVED_AND_DEFERRED_FEATURES.md](REMOVED_AND_DEFERRED_FEATURES.md).

Do not treat the current UI or `IDatabase` shape as the final API. They are a working vertical slice
used to evolve the architecture incrementally.
