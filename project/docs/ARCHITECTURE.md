# Ticket Hub architecture

## 1. Style

Ticket Hub uses a modular monolith with ports-and-adapters boundaries. It is one deployable server initially, but major capabilities have explicit interfaces so storage, database, mail, jobs, cache, search, events, secrets and observability can have multiple implementations.

The application must not become a generic SQL abstraction. Database ports express domain operations and transactional use cases. PostgreSQL and SQLite adapters are free to use their native concurrency and indexing features behind those ports.

## 2. Namespace and module map

```text
TicketHub
├── Common                 UUIDs, time, checksums, result/error utilities
├── Config                 validated configuration and feature flags
├── Domain
│   ├── Identity
│   ├── Authorization
│   ├── Projects
│   ├── Issues
│   ├── Workflow
│   ├── Agile
│   ├── Collaboration
│   ├── TimeTracking
│   ├── Releases
│   └── Audit
├── Application            use cases, transactions, authorization orchestration
├── Ports
│   ├── Database
│   ├── Search
│   ├── Storage
│   ├── Mail
│   ├── Jobs
│   ├── Events
│   ├── Cache
│   ├── Secrets
│   └── Observability
├── Infrastructure
│   ├── Database/PostgreSQL
│   ├── Database/SQLite
│   ├── Search/PostgreSQL
│   ├── Search/SQLiteFts5
│   ├── Storage/Filesystem
│   ├── Storage/S3
│   ├── Mail/Smtp
│   ├── Mail/Sendmail
│   └── ...
└── Web
    ├── Http
    ├── ApiV1
    ├── Html
    ├── Security
    └── Sse
```

The current prototype still has a smaller `Application -> IDatabase` shape. Refactoring toward this map is incremental; no rewrite is required.

## 3. Request flow

```text
HTTP request
  -> security middleware
  -> authenticated Principal
  -> route/controller DTO validation
  -> application use case
  -> authorization policy
  -> database transaction
  -> append issue history / audit / outbox event
  -> commit
  -> response DTO
  -> asynchronous jobs and SSE projection
```

Controllers never construct SQL and database adapters never decide user-facing authorization policy.

## 4. Transaction and event rules

A user-visible mutation and its structured history, audit record and durable event/outbox row commit in one database transaction. Email, webhooks, search updates and external side effects run after commit from durable jobs.

PostgreSQL workers claim jobs with row locking such as `FOR UPDATE SKIP LOCKED`. SQLite runs a bounded local worker and single process.

The durable event log is the recovery source. PostgreSQL `LISTEN/NOTIFY` is only a wake-up signal; losing a notification must not lose the event.

## 5. Database boundary

The logical model is shared but physical SQL is backend-specific.

PostgreSQL may use:

- `TIMESTAMPTZ`,
- partial and GIN indexes,
- advisory locks,
- `LISTEN/NOTIFY`,
- `SKIP LOCKED`,
- native full text,
- online snapshot mechanisms.

SQLite may use:

- WAL,
- `BEGIN IMMEDIATE`,
- FTS5,
- one-process coordination,
- short read-only maintenance windows.

Cross-backend behavior is verified at the application-contract level, not by forcing identical SQL.

## 6. Migration discipline

- Schema migrations are ordered files and immutable after release.
- Applied migrations store a content checksum.
- A changed historical migration is a startup error.
- PostgreSQL uses a cross-instance advisory migration lock.
- Long migrations use expand/migrate/contract phases to permit rolling upgrades.
- Demo seed data is not a schema migration.

## 7. Concurrency

Issues and other edit-heavy aggregates carry a monotonically increasing version. Updates include the expected version. Stale writes fail with a conflict response; the UI compares server and local changes.

Project issue-number allocation is transactional. Numbers may be consumed by failed operations and are never reused.

## 8. Authorization

Application use cases receive a `Principal` containing user UUID, authentication method, token scopes and session/service-account identity. Permission evaluation combines:

- global administrator status,
- project permission scheme,
- groups,
- project roles,
- contextual grants such as reporter/assignee,
- API scopes.

A token scope can reduce permission but cannot increase it.

## 9. Frontend

Crow serves semantic HTML for stable routes. JavaScript modules enhance pages using `/api/v1`, SSE and accessible dialogs. The UI keeps URL state navigable and reloadable.

No framework-specific virtual DOM or build-time SPA runtime is required. A small bundling/minification step may be introduced later, but source remains standards-based modules.

## 10. Security boundaries

- Secrets are resolved through a secrets port and redacted from logs.
- Markdown and inbound HTML are sanitized with an allowlist.
- Attachments use safe response headers and internal IDs, not filesystem paths.
- External images are blocked by default.
- Webhooks are signed and retried from a durable queue.
- Browser writes use CSRF protection in addition to `SameSite` cookies.
- API writes use tokens and idempotency where required.
- Security headers and trusted-proxy configuration are explicit.

## 11. Deployment topology

### Small installation

```text
Ticket Hub process
├── SQLite file
├── local attachment directory
├── local worker
└── SMTP or sendmail
```

### Production installation

```text
Load balancer
├── Ticket Hub instance A
├── Ticket Hub instance B
└── Ticket Hub instance N
        |
        +-- PostgreSQL
        +-- S3-compatible storage
        +-- SMTP/provider API
        +-- optional Redis/broker later
```

Instances are stateless except for bounded local cache and live SSE connections.

## 12. Build targets

Planned CMake targets:

- `ticket-hub-domain`
- `ticket-hub-application`
- `ticket-hub-database-common`
- `ticket-hub-database-sqlite`
- `ticket-hub-database-postgresql`
- `ticket-hub-web`
- `ticket-hub-server`
- `ticket-hub-cli`
- focused unit/integration test targets

The existing `ticket-hub-core` target remains until this split is justified by implemented modules.
