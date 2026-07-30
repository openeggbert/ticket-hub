# Scope status

The approved product scope is [../SPECIFICATION.md](../SPECIFICATION.md). The implementation roadmap is [ROADMAP.md](ROADMAP.md), and the target table catalog is [DATA_MODEL.md](DATA_MODEL.md).

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

## Not yet production-ready

- Writes still use a fixed demo identity.
- No password, OIDC, session or token implementation.
- No permission enforcement.
- Status changes are not yet restricted by the approved workflow engine.
- Custom fields, saved filters, persistent boards, sprints and reports are not implemented.
- Attachment storage, notifications, email, jobs, event distribution and webhooks are not implemented.
- PostgreSQL runtime integration tests require an external test server and are not included in this sandbox verification.

Do not treat the current UI or `IDatabase` shape as the final API. They are a working vertical slice used to evolve the architecture incrementally.
