# Ticket Hub V1 product specification (reduced scope)

Status: **approved V1 baseline**, superseding `SPECIFICATION.md` as the day-to-day build target.
Product name: **Ticket Hub**
Primary C++ namespace: **`TicketHub`**
License: **MIT**

`SPECIFICATION.md` remains the long-term aspirational product baseline and is not deleted — it is
useful for post-V1 planning. This document is what actually gets built first: a small, finishable,
self-hosted issue tracker, produced by walking all 142 decisions in `docs/PRODUCT_DECISIONS_COMPLETE.md`
again with the product owner and re-classifying each one (`docs/REDUCED_SCOPE_DECISIONS.md`).
Every section below cites the decision numbers (`D#`) it rests on.

Ticket Hub V1 is a self-hosted, single-tenant issue tracker for one internal software team, implemented
in C++20 with Crow and a hybrid vanilla HTML/CSS/JavaScript frontend, with **both PostgreSQL and SQLite**
kept at full feature parity (D137).

## 1. Product boundary

Single-tenant self-hosted instance; no multi-tenant organizations table (D0). Software projects only;
no Business/Service Management projects, ever (D6). Company-managed configuration only — there is no
team-managed mode and no plan to add one (D8). Each issue belongs to exactly one project (D63).

## 2. Deployment

Docker image + Docker Compose (with PostgreSQL) is the only supported distribution path for V1. No
`.deb`/`.rpm` packages, no Kubernetes/Helm chart, no horizontal (multi-instance) scaling (D50). Linux is
the fully supported platform; Windows/macOS remain best-effort buildable with no support commitment
(D138). Both PostgreSQL (primary) and SQLite (full feature parity, single-process) are supported
end-to-end (D137).

## 3. Identity and authentication

- **Local accounts only.** No OpenID Connect / OIDC of any kind in V1 — not even a reserved extension
  point (D1). Users are identified by an immutable UUID with a unique email and an optional unique
  handle (used for @mentions); display name is not unique (D56).
- **Registration:** administrator-created accounts only. No public self-registration, no invitation-token
  flow (D2).
- **Passwords:** Argon2id, strength checks, login rate limiting, temporary lockout, and full session
  invalidation on password change are all kept as non-negotiable security basics (D53). Password reset
  is **administrator-performed** (sets a temporary password) rather than self-service email reset,
  because there is no registration self-service and no email backend (D2, D52, D53).
- **Sessions:** server-side cookie sessions for the browser (HttpOnly/Secure/SameSite, CSRF protection,
  rotation, expiry, active-session management) (D54). API clients use a single class of **personal
  access token (PAT)**: hashed storage, expiration, revocation, last-used tracking; no scopes (a token
  always has exactly its owner's permissions), no rotation, no admin-configurable max lifetime, no
  service accounts (D39, D40, D54).
- **User lifecycle:** accounts can be deactivated. No anonymization, no account merge (D57).
- **Anonymous access:** optional, disabled by default, read-only browse of projects/issues/public
  attachments when explicitly enabled — kept from the original plan despite being somewhat orthogonal
  to the admin-only registration model (D2, D59).

## 4. Authorization

Fixed, small set of built-in project roles (e.g. Admin / Member / Viewer) instead of Jira-style
configurable permission schemes. No grant targets on reporter/assignee/anonymous beyond the anonymous
read toggle in §3 (D3). Project visibility has no separate Private/Internal/Public flag: every
authenticated user can see every project; roles control who can *edit*, not who can *see* (D58).
Issue-level security is out of scope permanently: anyone who can see a project sees every live issue
in it (D21).

## 5. Projects

- Project keys: 2-12 uppercase letters/digits, start with a letter, `^[A-Z][A-Z0-9]{1,11}$`,
  case-insensitive on input, canonical uppercase output (D93, D94) — already implemented.
- Issue numbering always starts at 1 for a new project (no admin-selectable start, since there is no
  Jira migration path to preserve numbering continuity for) (D95). Gaps are allowed; numbers are never
  reused (D92, D96) — already implemented.
- Changing a project's key is allowed; the old key becomes a permanent alias (D91) — built on the
  existing alias mechanism (D38, D90).
- Project moves between projects are **always allowed**: every project shares the same fixed issue
  types, fixed workflow, and fixed field set, so there is no compatibility check to run (D37).
- **Archival:** read-only archive using the existing `archived`/`archived_at` columns, with write-time
  enforcement (D87).
- **Deletion:** recycle bin with a fixed 90-day retention, checked on demand (no background sweep job,
  no admin-configurable retention) (D89). Permanent deletion no longer requires a prior verified export
  — that precondition was dropped once the Jira migration tool was removed (D48, D88). The project key
  stays reserved for the whole time a project is in the recycle bin (D90) — already implemented.
- **Components:** simple, one per issue (name, description, lead, default assignee), unchanged from the
  original plan (D19).
- **Versions/releases:** **not in V1** — no Fix Version/Affects Version, no release pages (D18).

## 6. Issue model

Fixed issue types: Epic, Story, Task, Bug, Sub-task — already implemented, no custom types (D29).
Hierarchy is fixed at Epic → Story/Task/Bug → Sub-task with no aspirational note about a level above
Epic (D5, D65, D66, D64). Fixed priorities (Highest/High/Medium/Low/Lowest) and fixed resolutions
(Fixed/Done/Won't Fix/Duplicate/Cannot Reproduce) — both already implemented (D27, D28).

Standard fields only: summary, Markdown description, type, status, priority, reporter, assignee,
Epic/parent, component, labels, **story points** (no time estimates — see §11), due date, watchers,
votes, links, attachments, comments, worklogs (simplified — see §11), and history. **No custom fields
of any kind in V1** — the standard field set is fixed (D9).

- **Labels:** free-form, autocomplete, case-insensitive normalization — already implemented (D97).
- **Deletion:** recycle bin, fixed 90-day retention, checked on demand — already implemented (D136).
- **Cloning:** simple field-copy clone (summary/description/type/priority/labels/component) into a new
  issue in the same project, creating a `clones`/`is cloned by` link; no selection dialog, no copying
  attachments/sub-tasks/links (D60).
- **Templates:** **not in V1** — use cloning instead (D61).
- **Recurring issues:** out of scope, permanently (D26).

## 7. Sub-task and hierarchy rules

Fixed, hardcoded rules replace the original configurable workflow validators/post-functions (a direct
consequence of the fixed workflow in §8, D4):

- A parent **cannot** transition to Done while any sub-task is unfinished (D68).
- Reopening a parent **leaves its sub-tasks unchanged** (no automatic reopen cascade) (D69).
- Reopening any issue **always clears its resolution** (D70).
- Resolution is set as a plain required field on the transition to Done, chosen manually — there is
  no post-function chain (D28, D70, D76, D77).

## 8. Workflow

**One fixed, built-in workflow for every project.** No workflow designer, no draft/publish cycle, no
configurable conditions/validators/post-functions/transition forms, no status categories admin UI, no
status-retirement migration wizard (D4, D71-D77). This single decision was the largest cost reduction
in the entire V1 scope (est. 60-100h). Status categories (To Do / In Progress / Done) exist only
implicitly in the hardcoded workflow.

## 9. Custom fields

**Not in V1.** Only the fixed standard field set from §6 exists. This is deferred, not removed — a
real dynamic field-type/context/rendering system may be worth building post-V1 (D9).

## 10. Search and filters

Visual, ad-hoc, in-UI filtering only (project/type/status/priority/assignee/labels/date range). No
saved filters, no sharing, and — because there's nothing to save — no quick filters either (D10, D35).
No JQL (never was in scope). Full-text search is a simple `LIKE`/`ILIKE` query against
summary/description, identical on PostgreSQL and SQLite, no `tsvector`/FTS5 indexes (D43).

## 11. Time tracking

Story points only, already implemented as a plain integer field. No original/remaining time estimates
(D12). Worklogs are simplified: time spent + comment, no automatic remaining-estimate adjustment, and
no separate own-vs-others edit/delete permission split — anyone with access to the issue can edit any
worklog on it (D13).

## 12. Boards and agile planning

**Kanban only.** No Scrum, no sprints, no backlog/active-sprint model (D7, D30, D67). One board is
automatically created per project; no multiple boards, no saved-filter-based boards, no multi-project
boards (D7). Board columns map 1:1 to the fixed workflow statuses, exactly as originally planned — this
was already the cheapest option (D32). WIP limits are soft/visual only, unchanged from the original plan
(D33). No swimlanes (board is a flat per-column list) and no quick filters (D34, D35). Manual ordering
uses a simple integer order column with renumbering on insert, replacing the original LexoRank-style
string rank (D31).

## 13. Reports

**No agile reports in V1** — no burndown, sprint report, velocity, cumulative flow diagram, or control
chart, even the Kanban-compatible ones. All of these either require sprints (removed, D7) or were
deferred to avoid a half-built report layer (D11).

## 14. Collaboration

### Comments

Single chronological discussion, no threads (D85) — already the cheapest option. Markdown storage with
the **full visual toolbar and live preview kept** (the user chose not to cut editor UX) (D16). Edited
comments show only an "(edited, timestamp)" flag — no stored version history of prior text (D81).
Deleted comments are tombstoned for normal users; content stays in the database (existing soft-delete
columns) and is visible directly to administrators, with no separate admin viewer UI (D82). Comment
permissions: an author can edit/delete their own comment; any admin can edit/delete any comment — no
separate 4-permission (edit-own/edit-all/delete-own/delete-all) matrix (D83). Fixed emoji reaction set
on comments is kept (D84).

### Mentions, watchers, votes

@handle mentions are kept in every Markdown field, with autocomplete and an in-app notification to the
mentioned user (D80). Watching is **self-service only** — no managing other users' watchers (D20).
Voting is kept as originally planned: one vote per authenticated user, visible count/voters, no
automatic priority change (D79).

### Links

Fixed, larger built-in link-type catalog: blocks/is blocked by, relates to, duplicates/is duplicated
by, clones/is cloned by. No admin-configurable link types (D17).

### Checklists

Markdown checklist syntax only (`- [ ]` / `- [x]`), already covered by the kept Markdown editor; no
separate checklist entities (D62).

## 15. Attachments

**Local filesystem storage only, hardwired** — no abstract storage port/interface and no S3 backend
in V1 or reserved for later (the user explicitly chose the cheaper hardwired option over an extension
point) (D15). The database still stores only metadata, never bytes.

- Fixed, non-configurable limits (e.g. 25 MB/file, 20 attachments/issue, blocked dangerous extensions);
  no admin configuration, no quotas (D98).
- All 4 preview types are kept, built on native browser elements (`<img>`, `<embed>`/`<iframe>` for PDF,
  `<audio>`/`<video>`) rather than heavy libraries like PDF.js (D99).
- Full upload + drag/drop + clipboard-paste support in the Markdown editor is kept (D100).
- Sortable list plus recycle bin is kept, fixed 90-day retention checked on demand (D101, D102).
- Duplicate filenames create independent immutable attachments — already implemented (D103). No
  physical deduplication by content hash — already the cheapest option (D104).
- Integrity is verified at upload time (SHA-256/size) only; **no periodic background integrity audit**
  (that would need job infrastructure, which is out of scope — see §18) (D105).

## 16. Notifications

Fixed, non-configurable in-app notifications only: assigned-to-me, mentioned, comment on a watched
issue. No notification schemes, no email delivery, no per-user preferences, no digests (D14, D86).
Password reset is admin-performed instead of email-based (§3). An in-app admin banner reports available
new Ticket Hub versions; no email delivery for that either (D112).

## 17. Automation

**None in V1.** No triggers/conditions/actions rule engine of any kind, not even a minimal one — every
action is performed manually by a user (D25).

## 18. API, background jobs, and realtime

- **REST API:** `/api/v1` prefix from day one; no formal deprecation policy until a real `/api/v2` is
  needed (D127). PAT-authenticated only — no public/anonymous API tokens, no webhooks, no service
  accounts (D39). Fixed, non-configurable rate limits per IP/user on login and write endpoints; no
  per-endpoint/service-account exceptions (D124). Fixed request/body/batch-size constants; no admin
  configuration (D125). Numbered/offset pagination everywhere; no cursor pagination (D126). No
  `Idempotency-Key` mechanism — accept the small risk of duplicate records on client retries (D128).
- **Concurrency:** optimistic locking is kept in full — issues carry a version, stale writes are
  rejected (409), and the UI offers a reload/reapply dialog on conflict (D129).
- **Background jobs:** **none.** No durable job queue of any kind. Everything (including the
  now-hardcoded attachment integrity check) runs synchronously inside the HTTP request (D51).
- **Realtime:** **none.** No Server-Sent Events, no polling fallback, no internal event distribution.
  Users refresh the page to see other users' changes (D130, D131).
- **Caching:** **none.** Every query hits the database directly; no in-memory cache layer (D132).
- **Git integration, third-party extensions, webhooks:** all out of scope — each depended on the public
  API/webhook/service-account infrastructure that was removed (D39, D42, D49).

## 19. Import/export, backup, and upgrades

- **Import/export:** simple **read-only CSV export** of issues only. No CSV import, and **no Jira
  migration tool** — that alone was one of the largest single cost reductions (est. 40-70h) (D48).
- **Backup:** copy the attachment directory plus a database dump; no separate checksummed manifest file
  (D106). Offline/maintenance-window backup only for both databases — no online consistent snapshot
  logic (D107).
- **Restore:** direct restore into the target database via `ticket-hub restore`, with a confirmation
  warning; the administrator is responsible for taking their own pre-restore backup. No isolated staging
  environment, no atomic switch (D108).
- **Backup versioning:** forward-migrate older backups through the normal migration system; reject
  newer backups in an older app — already a natural consequence of the existing migration system
  (D109). No LTS bridge-version planning for "very old" backups — premature for a product with no
  prior releases (D110).
- **Upgrades:** the existing `ticket-hub migrate` command is the entire upgrade mechanism — no separate
  `upgrade check`/`upgrade apply` wizard, no rolling upgrades (D111).
- **Release channels:** none — a single version line with semver tags; no Stable/LTS/Preview split
  (D113).
- **Database backend migration:** none — the administrator picks PostgreSQL or SQLite at install time
  and stays on it; no SQLite↔PostgreSQL migration tool (D114).
- **Inbound email:** **entirely out of scope** — no IMAP/pipe/webhook adapters, no reply-to-comment,
  no sender verification, no idempotency/quarantine/dead-letter handling. This whole subsystem (the
  original decisions 115-123) is moot because outbound email itself was removed (D52, D115-D123).

## 20. Observability, secrets, and encryption

Structured JSON logs to stdout only — no Prometheus metrics endpoint, no OpenTelemetry tracing
(D133). Secrets (DB password, session secret, etc.) come from environment variables / `.env` file /
Docker secrets only — no pluggable backend, no encrypted-in-DB values, no Vault integration (D134).
Encryption at rest relies entirely on infrastructure/disk-level encryption managed outside the
application; there is no application-level encryption of attachments or fields in V1 (D135).

## 21. Localization, appearance, and accessibility

**English only, no i18n framework in V1** — all text is hardcoded in UI and templates. No Czech
translation, no resource-file extensibility (D44). Light and dark theme only — no high-contrast theme,
no installation branding (logo/name/accent/login page) (D46). Accessibility target is a reasonable
baseline (semantic HTML, keyboard operability) without committing to or tracking a formal WCAG
compliance level or a dedicated audit/testing deliverable (D47). UTC storage plus full per-user
timezone handling (auto-detect, manual override, 12/24h preference, locale date format, DST
correctness) is kept in full — it was already cheap on top of mandatory UTC storage (D45).

## 22. Frontend architecture

Unchanged, non-negotiable, already implemented: Crow serves real HTML pages/URLs; vanilla JavaScript
progressively enhances navigation, boards, dialogs, filters, and forms via `/api/v1`. No React, Vue,
Angular, or other frontend framework. No offline-capable PWA. The only change from the original plan
is that the "SSE" item in the enhancement list is now moot, since realtime updates were removed (§18)
(D140).

## 23. Explicit non-goals for V1

Everything the original `SPECIFICATION.md` §22 already excluded, **plus** everything reclassified as
`REMOVE_COMPLETELY` or `DEFER_AFTER_V1` above. See `docs/REMOVED_AND_DEFERRED_FEATURES.md` for the
complete, decision-numbered list. The headline removals:

- OpenID Connect (any provider), service accounts, webhooks, public/anonymous API beyond PAT.
- The entire configurable workflow engine (designer, conditions, validators, post-functions, drafts).
- Configurable permission schemes and notification schemes (replaced by fixed roles / fixed rules).
- Custom fields, saved/shared filters, quick filters, swimlanes.
- Scrum/sprints and every agile report.
- Versions/releases, issue templates, automation rules.
- The Jira migration tool, CSV import, application-level backend migration (SQLite↔PostgreSQL).
- Inbound email (the entire subsystem), outbound SMTP delivery.
- Background job queue, internal event bus, SSE/realtime, in-memory cache.
- Kubernetes/Helm, `.deb`/`.rpm`, horizontal scaling, S3 attachment storage.
- i18n framework, high-contrast theme, installation branding, formal WCAG compliance tracking.
- Prometheus/OpenTelemetry, pluggable secrets backend, application-level encryption.

## 24. Non-negotiable data invariants (unchanged)

Identical to `SPECIFICATION.md` §23 — these were never up for reduction:

1. Every persistent entity uses an application-visible immutable UUID.
2. Every issue belongs to exactly one project.
3. Issue and project key input is case-insensitive; canonical output is uppercase.
4. A key or issue number never resolves to a different entity later.
5. Old issue keys remain permanent aliases after moves or project-key changes.
6. Sequence gaps are allowed; sequence reuse is forbidden.
7. History and audit are structured, append-oriented data.
8. Deleted user-facing data uses a recycle-bin/tombstone stage before permanent removal where specified.
9. Database and storage adapters do not leak engine-specific SQL into application services.
10. Cache, search indexes, and event delivery are derived infrastructure, not sources of truth — moot
    in V1 since none of the three exist, but the principle still applies to anything built later.
