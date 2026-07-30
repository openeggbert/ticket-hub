# Ticket Hub product specification

Status: approved product baseline  
Product name: **Ticket Hub**  
Primary C++ namespace: **`TicketHub`**  
License: **MIT**

Ticket Hub is a self-hosted, Jira-like software issue tracker implemented in C++20 with Crow and a hybrid vanilla HTML/CSS/JavaScript frontend. PostgreSQL is the primary production database. SQLite provides the same user-facing product features for smaller single-process installations.

This document records the agreed product scope. It is the source of truth when an older prototype document or implementation detail disagrees with it.

## 1. Product boundary

Ticket Hub is a **single-tenant installation**, similar to a self-hosted Jira Data Center instance. One installation contains many users, groups, projects, boards, workflows and issues. It does not host multiple isolated customer organizations in one database.

The first product supports only **company-managed Software projects**. Team-managed projects may be added later. Business projects, Service Management, customer portals and SLA functionality are not part of the initial product.

Each issue belongs to exactly one project. Boards and saved filters may display issues from multiple projects.

## 2. Supported deployment modes

- Linux is the fully supported production server platform.
- Native binaries, `.deb`, `.rpm`, an official Docker image, Docker Compose and a Kubernetes Helm chart are planned deliverables.
- Windows and macOS remain buildable and runnable on a best-effort basis.
- PostgreSQL supports multiple Ticket Hub instances, multiple workers, online backups and rolling upgrades where schema compatibility permits.
- SQLite supports the same user-facing features, but only one Ticket Hub server process, limited worker concurrency and no horizontal scaling.

## 3. Identity and authentication

Ticket Hub supports local accounts and OpenID Connect.

### Local accounts

- Users are identified internally by immutable UUID.
- Email is unique.
- A unique handle is optional and is primarily used for mentions.
- Display names are not unique.
- Passwords use Argon2id.
- Password strength checks, rate limiting, temporary lockout, email reset and invalidation of all sessions after password change are required.
- MFA and WebAuthn are not in the initial scope.

### Registration

The installation administrator chooses one of these modes:

- public registration,
- invitation-only registration,
- administrator-created accounts.

Invitation-only is the default.

### OpenID Connect

Each OIDC provider can configure:

- automatic account creation or pre-existing accounts only,
- allowed email domains,
- default groups,
- OIDC-group to Ticket Hub-group mapping.

Periodic directory deprovisioning is deferred.

### Sessions and tokens

- Browser UI uses secure database-backed server sessions in `HttpOnly`, `Secure`, `SameSite` cookies.
- Users can inspect active sessions and sign out one or all devices.
- API clients use scoped personal access tokens or service-account tokens.
- Tokens support scopes, expiration, revocation, rotation, last-used tracking, administrator maximum lifetime and audit records.
- A token can never exceed its owner's ordinary permissions.

### User lifecycle

Accounts can be deactivated, anonymized and merged. Merging transfers ownership and historical references to a surviving account without destroying issue history.

## 4. Authorization

Ticket Hub uses Jira-like **permission schemes** assigned to projects.

Permission grants may target:

- users,
- groups,
- project roles,
- reporter,
- assignee,
- project lead,
- all authenticated users,
- anonymous users when globally enabled.

Project visibility is expressed only through permission schemes; there is no competing Private/Internal/Public flag.

Anonymous access is disabled by default. When enabled, it is read-only and may cover project browsing, issue viewing and explicitly public attachments. Anonymous creation, comments and modification are not allowed.

Issue-level security schemes and private issues are not in the initial scope. Anyone with `Browse Project` can see all live issues in that project.

Comment permissions are separate: edit own, edit all, delete own and delete all.

## 5. Projects

A project contains software-development configuration and data.

- Project keys are 2–12 characters, match `^[A-Z][A-Z0-9]{1,11}$`, and are case-insensitive on input.
- Keys are stored canonically in uppercase.
- A project can choose its initial issue number; the default is 1.
- Sequence numbers never decrease or get reused. Gaps are allowed.
- Changing a project key changes current issue keys while preserving all previous keys as permanent aliases.
- Project keys remain reserved while a project is in the recycle bin.
- Archived projects are read-only, hidden from normal active lists and restorable.
- Permanent project deletion requires a successful complete project export first.
- Deleted projects enter a recycle bin with configurable retention, default 90 days.

### Components

A project supports simple components. A component has a name, description, lead and default assignee. An issue belongs to at most one component.

### Versions and releases

Project versions support:

- name, description and release date,
- unreleased, released and archived states,
- release notes,
- completion progress,
- completed and incomplete issue lists,
- multiple Fix Version and Affects Version values,
- release, archive and merge operations.

Deployment environments and native CI/CD integration are deferred.

## 6. Issue model

The fixed initial issue types are:

- Epic,
- Story,
- Task,
- Bug,
- Sub-task.

Custom issue types and issue-type schemes are not initially supported.

### Hierarchy

- Story, Task and Bug may belong to zero or one Epic.
- Issues without an Epic appear in a `No Epic` backlog group.
- Sub-task must have a Story, Task or Bug parent.
- Sub-task cannot exist independently, cannot have children and must stay in the same project as its parent.
- Sub-task inherits the parent's sprint and appears nested under the parent.
- Whether a parent can complete with unfinished sub-tasks is controlled by workflow validators.
- Reopening or changing child statuses may be controlled by workflow post-functions.

### Fixed priorities and resolutions

Priorities:

- Highest,
- High,
- Medium,
- Low,
- Lowest.

Resolutions:

- Fixed,
- Done,
- Won't Fix,
- Duplicate,
- Cannot Reproduce.

Status and Resolution remain separate concepts. Workflow post-functions clear, preserve or set Resolution.

### Standard issue capabilities

Issues support summary, Markdown description, type, status, priority, reporter, assignee, Epic/parent, component, labels, story points, time estimates, due date, versions, custom fields, watchers, votes, links, attachments, comments, worklogs and history.

Issue keys are case-insensitive on input and canonical uppercase on output. Old keys are permanent aliases. Issue numbers are never reused, even after permanent deletion.

### Labels

Labels are free-form, multiple per issue, case-insensitive and stored in a normalized canonical form. The UI autocompletes labels already used in the project.

### Deletion

Issues use a recycle bin. Retention is installation-configurable and defaults to 90 days. Restoration and permanent deletion require permission and are audited.

### Cloning and templates

Cloning can copy selected fields, attachments, issue links, sub-tasks, sprint, Epic, assignee, versions and custom fields. It may target another compatible project and creates a `clones / is cloned by` link.

Global and project issue templates can prefill fields and optionally create sub-tasks, links, Markdown checklists and watchers.

Recurring issues are not in the initial scope.

## 7. Workflow

Ticket Hub implements shared Jira-like workflow schemes for company-managed projects.

A workflow defines:

- custom statuses,
- transitions,
- transition conditions,
- validators,
- ordered post-functions,
- a simple transition form.

Every status belongs to exactly one fixed category:

- To Do,
- In Progress,
- Done.

Administrators may create statuses but not categories.

### Conditions

Conditions can use project roles, groups, users, assignee, reporter, project lead, field values, previous status and safe AND/OR combinations. Custom scripts are not supported.

### Validators

Validators may enforce required fields, completed sub-tasks, assignee or Fix Version, numeric/date constraints, permissions, field conditions, attachment/comment counts, required links, worklogs and related-issue status checks. Validators can be combined. Custom scripts are not supported.

### Post-functions

Transitions contain ordered, safe actions such as field updates, resolution handling, assignee changes, comments, labels, notifications, estimate updates and sub-task actions. Mandatory internal state/history actions cannot be removed.

### Transition forms

Each transition directly defines its fields, order, required flags, defaults, comment field and help text. Ticket Hub does not implement Jira screen schemes, screen tabs or issue-type screen schemes.

### Draft and publication

A used workflow has one published version and an editable draft. Publishing requires impact review and any status migration. Previous published versions remain for audit and controlled rollback.

Removing a used status requires a migration wizard that maps affected workflows and issues to replacement statuses and records a complete audit trail.

## 8. Custom fields

Ticket Hub supports custom fields without Jira's general screen system.

Initial field types include text, Markdown, integer, decimal, Boolean, date, date-time, single select, multi-select, user, multi-user, project, issue link and URL.

A field can configure:

- name, description and type,
- default value,
- required/hidden state,
- order,
- active state,
- project and issue-type contexts,
- `show_on_create`, `show_on_edit`, `show_on_view`.

Custom fields participate in visual filters, search where supported, boards, webhooks and workflow rules. Selected sensitive fields may be application-encrypted, with reduced search/sort/filter capability.

## 9. Search and saved filters

The initial product uses visual/form-based filters only; it does not implement JQL.

Filters cover standard and custom fields, projects, types, statuses, priority, assignee, reporter, sprint, Epic, versions, labels and date ranges. Filters are stored as structured conditions, can be saved and shared, and can be used as board sources, quick filters and webhook conditions.

Full-text search uses backend-specific native implementations:

- PostgreSQL `tsvector` and GIN,
- SQLite FTS5.

Search remains behind a common interface.

## 10. Boards and agile planning

Ticket Hub supports multiple Scrum and Kanban boards. A board is based on a saved filter and may include issues from multiple projects.

- Board columns map one-to-one to workflow statuses.
- Moving an issue to a column performs the transition to that status.
- Global Jira-like Rank provides drag-and-drop ordering without renumbering the entire backlog.
- Every drag-and-drop operation has an accessible non-drag alternative.
- Kanban WIP limits are soft warnings, not hard blocks.
- Swimlanes may group by Epic, assignee, project, priority or issue type.
- Board administrators configure quick filters through the visual filter builder.

### Scrum

- A Scrum board has backlog, planned sprints and one active sprint.
- Parallel active sprints are not initially supported.
- Sprint supports goal, start/end dates and planned/active/completed states.
- Capacity can use story points, time or issue count.
- Closing a sprint moves incomplete work to backlog or a selected future sprint.
- Scope-change history is retained.

### Reports

Initial agile reports:

- burndown chart,
- sprint report,
- velocity chart,
- cumulative flow diagram,
- control chart,
- release burndown.

Report calculations use structured issue and sprint history rather than parsing display text.

## 11. Time tracking

Ticket Hub supports story points and time estimates.

- original estimate,
- remaining estimate,
- logged time,
- worklogs with work date and comment,
- automatic, unchanged or manually supplied remaining-estimate adjustment,
- permissions for editing/deleting own or others' worklogs.

Worklog visibility restrictions are not initially supported.

## 12. Collaboration

### Comments

Comments are a single chronological discussion without nested threads. Users can link to or quote a comment.

- Markdown storage with visual toolbar and preview.
- Full immutable edit-version history.
- Deletion leaves a tombstone; original content is available only to authorized administrators through audit records.
- A fixed emoji set is available on comments only.
- User mentions work in every Markdown-capable field and notify the mentioned user.
- Group and role mentions are deferred.

### Watchers and votes

Users can watch/unwatch issues. Authorized users can manage other watchers. Each authenticated user can add one public vote per issue; vote count and voters are visible. Votes do not automatically change priority.

### Links

Administrators define bidirectional issue-link types with outward and inward descriptions. Hierarchy is stored separately from ordinary issue links. Automatic dependency scheduling is deferred.

### Markdown checklists

Checklist syntax is ordinary Markdown only. There are no separate checklist entities or assignees.

## 13. Attachments

Attachment bytes use a pluggable storage interface. Initial backends:

- local filesystem,
- S3-compatible object storage.

The database stores metadata and a storage/object key, never the file bytes.

- Every upload creates an independent immutable object, even for identical content or duplicate filename.
- SHA-256 and size are recorded and checked at upload.
- Periodic background audits detect missing, changed or corrupted objects.
- Configurable limits cover file size, number per issue, MIME allow/deny lists, blocked extensions and storage quotas.
- Antivirus and DLP are not initially supported.
- Safe previews cover images, PDFs, text/source, and browser-supported audio/video.
- Other files are download-only.
- Images pasted or dragged into Markdown become ordinary attachments referenced by `attachment://<uuid>`.
- Unrestricted external image embedding is not supported.
- Attachment lists can sort by name, size, date, author or type.
- Deleted attachments enter a recycle bin with configurable retention, default 90 days.

## 14. Notifications and email

Projects use shared notification schemes. Rules map events to assignee, reporter, watchers, project roles, groups or users. Delivery channels are in-app and email.

Users configure non-mandatory event/channel preferences and immediate or digest delivery. Security and critical administrative notices cannot be disabled.

Outbound email uses a pluggable backend:

- SMTP,
- local sendmail,
- provider APIs later.

### Inbound email

Pluggable inbound adapters:

- IMAP with IDLE and polling fallback,
- local mail pipe,
- provider HTTP webhook later.

Known active users can create issues by email and reply to notification emails to add comments. Unknown senders are rejected.

- `text/plain` is preferred; sanitized HTML converts to Markdown as fallback.
- Replies use a fixed “reply above this line” marker with conservative quote stripping fallback.
- Attachments obey ordinary attachment rules.
- Processing is idempotent using Message-ID, provider/IMAP identifiers, normalized-content checksum and explicit processing states.
- Suspicious duplicates enter administrative quarantine.
- Transient failures retry with backoff; permanent failures enter a dead-letter queue.
- Creation of the issue/comment and accepted attachments is atomic.

## 15. Automation

The initial product supports simple built-in automation rules, not a general Jira Automation builder.

Supported triggers include issue creation, field change, transition, sprint change, comment and due-date events. Conditions use visual structured rules. Actions include field update, assignment, comment, transition, labels and notification. No custom scripts are executed.

## 16. API, webhooks and external apps

The public REST API is versioned by major URL, for example `/api/v1`. Within a major version only backward-compatible additions are permitted. Breaking changes require a new major version with documented deprecation and support periods.

- Public API supports scoped personal and service-account tokens.
- Cursor pagination is preferred; numbered/offset pagination is available where suitable.
- High-risk write operations require `Idempotency-Key`.
- Optimistic locking rejects stale writes and provides conflict information.
- Multi-level rate limits apply by IP, user/token, service account, endpoint, operation and installation.
- Endpoint-specific body, item-count, filter-complexity and page-size limits are enforced.
- Audited service-account exceptions can receive higher limits.

Webhooks are configurable by event, project and visual issue filter. Payloads are fixed, versioned JSON and signed. Delivery has retries, history and manual replay.

Third-party extensions run as external services and integrate through REST, webhooks, service accounts and declared UI links/panels. Ticket Hub does not load untrusted dynamic C++ plugins into the server process.

Lightweight Git linking accepts externally supplied commit, branch, pull/merge request and build links containing issue keys. Native repository connections and automatic workflow transitions are deferred.

## 17. Realtime behavior and concurrency

- Issue writes use optimistic locking and a version value.
- The UI shows competing changes and lets the user reload or reapply edits.
- Server-Sent Events provide near-real-time issue, comment, board, sprint and notification updates, with polling fallback.
- A pluggable internal event backend distributes events between instances.
- Initial PostgreSQL implementation uses a durable database event log plus `LISTEN/NOTIFY` wakeups.
- SQLite uses a local event loop.

## 18. Jobs, cache and observability

Background jobs use a pluggable durable queue. PostgreSQL supports multiple workers and nodes. SQLite uses a simpler single-instance worker. Redis may be added later.

Caching is pluggable. The default is per-instance memory cache with event-bus invalidation. Critical or rapidly changing data bypasses cache. Database remains the source of truth.

Observability is pluggable and supports:

- structured JSON logs,
- Prometheus metrics,
- OpenTelemetry traces,
- correlation IDs across HTTP, database, jobs, email and webhooks.

## 19. Audit, backup and lifecycle

Ticket Hub maintains structured per-issue history and a global administrative audit log. Audit categories have configurable retention and export. Ordinary read/view events are not logged by default.

Complete backups separate database/configuration data from attachment objects and tie them together with a checksummed manifest.

- PostgreSQL supports online consistent snapshot backup.
- SQLite defaults to a short read-only backup mode.
- Offline/read-only backup is available for both.
- Restore occurs into an isolated temporary environment, verifies manifest, checksums, schema and integrity, then atomically switches.
- Older supported backups migrate forward automatically.
- Newer backups are rejected by older applications.
- Very old backups use documented LTS bridge versions.
- Database-neutral application export/import enables SQLite ↔ PostgreSQL migration.

Upgrades use staged `ticket-hub upgrade check` and `ticket-hub upgrade apply`. PostgreSQL/Kubernetes may use rolling upgrades when schema compatibility allows; SQLite uses a maintenance window.

Ticket Hub only notifies administrators of available releases; it does not self-update. Release channels are Stable, LTS and Preview.

## 20. Localization, appearance and accessibility

- General resource-based i18n from the beginning.
- English is default; Czech is complete.
- Additional languages require resource files, not C++ changes.
- Timestamps are stored in UTC.
- Per-user time zone, browser detection, manual override, locale date format, 12/24-hour preference and DST correctness are required.
- Date-only values remain date-only.
- Light, dark and high-contrast themes.
- Limited installation branding: name, logo, favicon, accent and login page.
- No arbitrary custom CSS.
- Accessibility target: WCAG 2.2 AA.

Official browser support is the latest two major versions of Chrome, Firefox, Edge and Safari. Older modern browsers may receive progressively degraded basic reading; advanced boards, SSE and administration are not guaranteed.

## 21. Frontend architecture

Crow serves real HTML pages and direct URLs. Vanilla JavaScript progressively enhances navigation, forms, modals, filters, boards and realtime behavior through the REST API.

Ticket Hub does not use React, Vue, Angular or another frontend framework. Offline-capable PWA synchronization is not in the initial scope.

## 22. Explicit non-goals for the initial product

- Multi-tenant organizations.
- Business or Service projects and customer portal.
- SLA calendars and timers.
- Team-managed projects.
- JQL.
- Jira screen schemes/tabs.
- Custom issue types, priority schemes or resolution administration.
- Issue-level security.
- Recurring issues.
- Parallel active sprints.
- Deep comment threads.
- Dynamic in-process third-party C++ plugins.
- Native full Git/CI/CD/deployment integration.
- Antivirus, DLP and office-document conversion.
- MFA, passkeys and WebAuthn.
- Full Jira Automation or custom administrative scripts.
- Offline editing and synchronization.

## 23. Non-negotiable data invariants

1. Every persistent entity uses an application-visible immutable UUID.
2. Every issue belongs to exactly one project.
3. Issue and project key input is case-insensitive; canonical output is uppercase.
4. A key or issue number never resolves to a different entity later.
5. Old issue keys remain permanent aliases after moves or project-key changes.
6. Sequence gaps are allowed; sequence reuse is forbidden.
7. History and audit are structured, append-oriented data.
8. Deleted user-facing data uses a recycle-bin/tombstone stage before permanent removal where specified.
9. Database and storage adapters do not leak engine-specific SQL into application services.
10. Cache, search indexes and event delivery are derived infrastructure, not sources of truth.
