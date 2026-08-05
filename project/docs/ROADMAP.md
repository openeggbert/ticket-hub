# Ticket Hub implementation roadmap

Every phase ends with a buildable, migratable and tested product. Later phases may add features but must not invalidate earlier data invariants.

## Phase 0 — baseline and governance

Deliverables:

- approved `SPECIFICATION.md`, architecture and target data model,
- C++20/CMake baseline,
- formatting, warnings, sanitizers and CI,
- ordered checksummed migrations,
- configuration validation,
- health/readiness/version endpoints,
- SQLite and PostgreSQL test harnesses.

Exit gate: clean build, migration idempotence and checksum tests, no unresolved product-scope questions.

## Phase 1 — identity and secure sessions

- users, groups, invitations and configurable registration,
- local Argon2id credentials and password reset,
- server-side sessions and active-session management,
- OIDC provider port and first implementation,
- deactivation, anonymization and account merge,
- audit for security operations.

Exit gate: no fixed demo identity in write use cases.

## Phase 2 — authorization and projects

- project CRUD, archive, recycle bin and key aliases,
- groups, project roles and permission schemes,
- authorization middleware and application policies,
- components and project members,
- project-key change and initial sequence configuration.

Exit gate: every route is explicitly public, authenticated or permission-checked.

## Phase 3 — issue core

- fixed issue types, priorities and resolutions,
- full issue create/read/edit with optimistic locking,
- Epic and sub-task hierarchy,
- normalized labels,
- recycle bin and permanent-key reservation,
- structured history and activity timeline,
- watchers, votes and configurable issue links,
- bulk change and compatible-project move.

Exit gate: all issue mutations are transactional and append structured history.

## Phase 4 — workflow engine

- statuses and fixed categories,
- workflows, drafts, publication and version history,
- schemes mapped to fixed issue types,
- conditions, validators and ordered post-functions,
- transition forms,
- status-removal migration wizard.

Exit gate: status cannot be changed outside the workflow engine.

## Phase 5 — custom fields and filters

- field definitions, options, contexts and layouts,
- dynamic create/edit/view forms,
- structured visual filter model,
- saved filters and sharing,
- PostgreSQL full text and SQLite FTS5,
- cursor and numbered pagination.

Exit gate: board/search/webhook filters use the same typed condition model.

## Phase 6 — agile

- persistent Scrum and Kanban boards,
- one-status-per-column mapping,
- global rank and accessible reorder alternatives,
- backlog, Epics and No Epic group,
- sprints and scope history,
- WIP warnings, swimlanes and quick filters,
- SSE updates with polling fallback.

Exit gate: deterministic reports can be recomputed from history.

## Phase 7 — time, versions and reports

- estimates and worklogs,
- components completed,
- versions/releases and release notes,
- burndown, sprint, velocity, cumulative flow, control and release-burndown reports,
- fixed personal dashboard.

## Phase 8 — collaboration and attachments

- Markdown editor, sanitizer, mentions and preview,
- comment version history, tombstones and reactions,
- attachment storage port,
- filesystem and S3 adapters,
- limits, previews, recycle bin and integrity audit.

## Phase 9 — notifications and email

- notification schemes and user preferences,
- in-app notifications and digests,
- SMTP/sendmail ports,
- inbound IMAP/mail-pipe processing,
- quarantine, idempotency and dead-letter queues.

## Phase 10 — API and integrations

- complete `/api/v1`, OpenAPI description and scopes,
- personal tokens and service accounts,
- idempotency records and multi-level rate limits,
- signed filtered webhooks and delivery history,
- external-app descriptors and UI panels,
- lightweight Git/development links.

## Phase 11 — automation, templates and migration

- safe built-in automation rules,
- global/project issue templates,
- CSV import/export,
- Jira migration tool with lossy-mapping report,
- database-neutral installation migration.

## Phase 12 — backup, upgrade and operations

- complete manifest-based backup/restore,
- online PostgreSQL and read-only SQLite backup modes,
- isolated verified restore and atomic switch,
- upgrade check/apply and rolling-compatible migrations,
- Stable/LTS/Preview channels,
- structured logs, Prometheus and OpenTelemetry.

## Phase 13 — packaging and hardening

- non-root container,
- `.deb` and `.rpm`,
- Docker Compose and Helm,
- threat model and security review,
- WCAG 2.2 AA review,
- browser E2E matrix,
- load, failover and recovery tests,
- English/Czech completeness.

## Current implementation checkpoint

**Stale.** This paragraph described the very first remediation batch and predates almost all
implementation work. This file is long-term reference only (see `CLAUDE.md`'s source-of-truth
hierarchy); it is not updated per batch and must not be read as current status. For the actual current
status, see `docs/REDUCED_SCOPE_ROADMAP.md` (the roadmap actually being built against) together with
`PLAN.md`'s "Current status" line and `NEXT.md`'s "The roadmap is now complete" section — as of the last
update (2026-08-05), the entire reduced-scope V1 roadmap (Phases 1-8) plus 15 further post-V1 batches are
complete, and no further work is queued pending an explicit product conversation.
