# Ticket Hub V1 architecture (reduced scope)

This supersedes `ARCHITECTURE.md` as the day-to-day build target. `ARCHITECTURE.md` remains the
long-term aspirational architecture for post-V1 phases and is not deleted. Every simplification below
traces back to a decision in `REDUCED_SCOPE_DECISIONS.md` (cited as `D#`).

## 1. Style

Still a modular monolith with domain/application ports and infrastructure adapters — that principle is
unchanged. What shrinks is the number of pluggable capabilities: V1 has **exactly one production
implementation** for every port except the database (which intentionally keeps two, D137). There is no
speculative multi-backend abstraction for storage, jobs, cache, mail, events, secrets, or observability
— those layers either don't exist in V1 or are hardwired, not pluggable (D15, D51, D132, D52, D131,
D134, D133).

## 2. Namespace and module map

```text
TicketHub
├── Common                 UUIDs, time, checksums, result/error utilities
├── Config                 validated configuration and feature flags
├── Domain
│   ├── Identity            users, sessions, PATs, roles           (D1, D3, D39, D40, D54, D56)
│   ├── Authorization        fixed role checks, no scheme engine     (D3, D58)
│   ├── Projects              projects, components, recycle bin      (D19, D87, D89)
│   ├── Issues                 issues, links, labels, sub-tasks      (D9, D17, D29, D64-D70, D97)
│   ├── Workflow (fixed)        one hardcoded workflow, no engine     (D4, D71-D77)
│   ├── Agile (Kanban only)      boards, columns, rank                (D7, D31-D34)
│   ├── Collaboration            comments, mentions, watchers, votes  (D16, D20, D79-D85)
│   ├── TimeTracking              story points, simple worklogs       (D12, D13)
│   └── Audit                      append-only admin/security log      (D23)
├── Application            use cases, transactions, authorization orchestration
├── Ports
│   ├── Database             (two implementations: PostgreSQL, SQLite — D137)
│   └── Storage               (attachments; ONE implementation, local filesystem — D15, not a real
│                               port, deliberately hardwired rather than abstracted)
├── Infrastructure
│   ├── Database/PostgreSQL
│   ├── Database/SQLite
│   └── Attachments/Filesystem
└── Web
    ├── Http
    ├── ApiV1                (PAT-authenticated only, no webhooks — D39)
    ├── Html
    └── Security
```

Compared to the target module map in `ARCHITECTURE.md`, V1 has **no** `Search` port (a plain
`LIKE`/`ILIKE` query lives directly in the Issues repository, D43), **no** `Mail` port (D52), **no**
`Jobs` port (D51), **no** `Events` port (D131), **no** `Cache` port (D132), **no** `Secrets` port
(env vars only, D134), and **no** `Observability` port (stdout JSON logs only, D133). `Sse` is removed
from `Web` (D130).

The current prototype's smaller `Application -> IDatabase` shape is closer to this reduced target than
to the original full-scope one; the incremental refactor described in `ARCHITECTURE.md` §2 remains
valid guidance, just aimed at a smaller module set.

## 3. Request flow

```text
HTTP request
  -> security middleware (session cookie or PAT)
  -> authenticated Principal
  -> route/controller DTO validation
  -> application use case
  -> fixed-role authorization check
  -> database transaction
  -> append issue history / audit record
  -> commit
  -> response DTO
```

There is no post-commit asynchronous stage: no jobs, no outbox, no event projection, no SSE push
(D51, D130, D131). Everything a mutation needs to do, it does inside the request/transaction.

## 4. Transaction rules

A user-visible mutation and its structured history/audit record commit in one database transaction —
unchanged. There is no durable event/outbox row and no post-commit async work, because there is nothing
left that consumes one: no email, no webhooks, no search-index rebuild, no cache invalidation (D51,
D52, D39, D43, D132).

## 5. Database boundary

Unchanged from `ARCHITECTURE.md` — this is the one place V1 deliberately keeps the full original
complexity (D137). PostgreSQL and SQLite both remain first-class, with backend-specific SQL/locking
behind one application contract. Two exceptions worth noting explicitly:

- **Full-text search** no longer diverges between backends: both use a plain `LIKE`/`ILIKE` query, so
  there is no `tsvector`/GIN vs. FTS5 split to maintain (D43).
- **Backup/restore** no longer diverges into an "online snapshot" path for PostgreSQL vs. a "read-only
  window" path for SQLite — both use the same offline/maintenance-window approach (D107, D108).

## 6. Migration discipline

Unchanged: ordered, checksummed, immutable-once-applied migrations; PostgreSQL advisory lock; demo seed
stays separate from schema migrations. This is core infrastructure the reduced scope does not touch.

## 7. Concurrency

Unchanged: issues carry a monotonically increasing `version`; stale writes are rejected with a
conflict response; the UI compares server and local state (D129). Project issue-number allocation
remains transactional; numbers are never reused (D92, D96).

## 8. Authorization

Simplified from `ARCHITECTURE.md` §8. Application use cases still receive a `Principal` (user UUID,
authentication method — session or PAT), but permission evaluation is now just:

- global administrator status, and
- a fixed project role (Admin / Member / Viewer) from project membership (D3).

There are no groups, no project-role *schemes*, no contextual grants for reporter/assignee/anonymous
beyond the single anonymous-read toggle (D58, D59), and no token scopes to intersect — a PAT always
carries exactly its owner's permissions (D40).

## 9. Frontend

Unchanged from `ARCHITECTURE.md` §9, minus SSE: Crow serves semantic HTML for stable routes; vanilla JS
modules enhance pages via `/api/v1`. No framework-specific virtual DOM, no build-time SPA runtime, no
live-update channel — users refresh to see others' changes (D130, D140).

## 10. Security boundaries

Unchanged core rules: Markdown/inbound HTML sanitized with an allowlist; attachments use safe response
headers and internal IDs, not filesystem paths; browser writes use CSRF protection plus `SameSite`
cookies. Two items from the original list no longer apply because their subsystems don't exist:
"webhooks are signed and retried from a durable queue" (no webhooks, D39) and "secrets are resolved
through a secrets port" (env vars directly, D134).

## 11. Deployment topology

V1 has exactly one topology — the original "production installation" (load balancer + multiple
instances + shared S3) is out of scope (D50, D15):

```text
Ticket Hub process
├── PostgreSQL (primary) or SQLite (single-process)
├── local attachment directory
└── (no worker, no queue, no cache, no mail transport, no event bus)
```

## 12. Build targets

Unchanged from `ARCHITECTURE.md` §12 — the planned CMake target split
(`ticket-hub-domain`/`application`/`database-common`/`database-sqlite`/`database-postgresql`/`web`/
`server`/`cli`) is still the right shape, since it was never coupled to the removed features. The
existing `ticket-hub-core` target remains until the split is justified by implemented modules, exactly
as before.
