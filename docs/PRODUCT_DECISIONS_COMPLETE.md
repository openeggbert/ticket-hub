# Complete product decision register

Status: final baseline reconstructed from the interactive product-definition conversation.  
This file preserves every numbered decision so development can continue after the chat is deleted. `../SPECIFICATION.md` is the thematic normative specification; this ledger records the chronological choices and intentional simplifications.

## How to resolve conflicts

Later decisions refine or narrow earlier decisions. The clearest example is hierarchy: Decision 5 keeps future extensibility above Epic, while Decision 29 fixes the initial issue types. Therefore the initial implementation is only Epic → Story/Task/Bug → Sub-task, with future extensibility left in the model. When this ledger and `SPECIFICATION.md` appear to differ, use the later decision and the explicit resolution notes in the specification.

## Decisions

### 0. Installation/organization model

**Chosen:** Single-tenant self-hosted instance

One installation represents one organization/company environment, similar to Jira Data Center. It contains many users and projects. No multi-tenant organizations table is required for the initial product; Jira Service Management customer organizations are out of scope.

### 1. Authentication providers

**Chosen:** Local accounts plus OpenID Connect

Local password accounts are supported. The architecture also supports OIDC providers such as Keycloak, Authentik, Microsoft Entra ID, or Google without replacing the local-account model.

### 2. Registration modes

**Chosen:** Configurable per installation; invitation-only by default

The administrator can select public registration, invitation-only registration, or administrator-created accounts. OIDC just-in-time provisioning is separately configurable.

### 3. Project authorization

**Chosen:** Jira-like permission schemes

Global permission schemes are assigned to projects. Grants may target users, groups, project roles, reporter, assignee, project lead, all authenticated users, and optionally anonymous users.

### 4. Workflow model

**Chosen:** Shared Jira-like workflow schemes

Workflows are defined globally and can be mapped to issue types/projects. The target includes statuses, transitions, conditions, validators, required transition fields, and ordered post-functions.

### 5. Issue hierarchy direction

**Chosen:** Jira-like base hierarchy with future extensibility above Epic

The conceptual model keeps Epic → standard issue → Sub-task. A later product may add levels above Epic, but Decision 29 fixes the initial issue-type set, so upper levels are not implemented initially.

### 6. Project types

**Chosen:** Software projects only

Business and Service Management projects are excluded from the initial scope. Ticket Hub focuses on software development, backlog, Scrum, Kanban, versions, and releases.

### 7. Board types and scope

**Chosen:** Scrum and Kanban; multiple boards; saved-filter based

A project may have multiple boards. A board uses a saved visual filter and may display issues from one or multiple projects.

### 8. Project management style

**Chosen:** Company-managed first; team-managed later

Initial projects use shared schemes and centrally managed configuration. The architecture should not block a future team-managed mode, but it is not implemented now.

### 9. Custom fields and screens

**Chosen:** Custom fields with contexts, no Jira screen schemes

Fields support project/issue-type contexts, ordering, required/hidden settings, defaults, and show-on-create/edit/view flags. No named screens, tabs, screen schemes, or issue-type screen schemes initially.

### 10. Issue searching

**Chosen:** Visual/form-based filters only

No JQL-like query language initially. Filters cover standard and custom fields, can be saved/shared, and can back boards and quick filters. Store filter conditions structurally, not as raw SQL.

### 11. Agile reports

**Chosen:** Basic Jira-like report set

Include burndown, sprint report, velocity, cumulative flow, control chart, and release burndown.

### 12. Estimation

**Chosen:** Story points and time estimates

Projects/boards may report by story points, time, or issue count. Original estimate, remaining estimate, and logged time are supported.

### 13. Worklogs

**Chosen:** Jira-like worklogs without visibility restrictions

Record time spent, work date, comment, and remaining-estimate behavior. Permissions distinguish editing/deleting own versus others’ worklogs. No role/group-restricted private worklogs initially.

### 14. Notifications

**Chosen:** Jira-like notification schemes with in-app and email delivery

Project schemes map events to assignee, reporter, watchers, roles, groups, or specific users.

### 15. Attachment storage

**Chosen:** Pluggable filesystem and S3-compatible backends

Database stores metadata and a storage/object key, not file bytes. Initial providers are local filesystem and S3-compatible object storage.

### 16. Rich text

**Chosen:** Markdown storage with visual toolbar and preview

Descriptions/comments remain portable Markdown. The editor supports formatting, links, tables, code, attachments, mentions, and preview. Rendered HTML must be sanitized.

### 17. Issue links

**Chosen:** Configurable bidirectional Jira-like link types

Administrators define outward/inward labels such as blocks/is blocked by. Hierarchy remains separate from general issue links. No automatic dependency scheduling.

### 18. Versions and releases

**Chosen:** Jira-like project versions and release pages

Support multiple Fix Version and Affects Version values, release notes, completion progress, completed/incomplete issue lists, release/archive/merge actions. Deployment and full CI/CD integration are deferred.

### 19. Components

**Chosen:** Simple project components; at most one per issue

Each component has name, description, lead, and default assignee. Unlike Jira’s multi-component model, the initial Ticket Hub issue has zero or one component.

### 20. Watchers

**Chosen:** Jira-like issue watchers

Users can watch/unwatch themselves. Users with the appropriate permission can add/remove other watchers.

### 21. Issue-level security

**Chosen:** Not implemented initially

Anyone with Browse Project permission can see all issues in that project. No private issues or issue security schemes.

### 22. Issue deletion

**Chosen:** Recycle bin / soft deletion

Deleted issues can be restored or permanently deleted by authorized administrators. Deletion actions are audited.

### 23. Audit

**Chosen:** Global administrative audit plus issue history, export, and retention

Audit configuration/user/security/workflow/project changes and important authentication events. Support export and category-based retention. Do not log every read by default.

### 24. Dashboards

**Chosen:** One fixed personal dashboard per user

No multiple dashboards, dashboard sharing, custom layouts, or plugin gadgets initially. Include assigned issues, watched issues, recent activity, active sprint, deadlines, and simple statistics.

### 25. Automation

**Chosen:** Simple built-in rules, not a full visual Jira Automation product

Provide common triggers, conditions, and fixed actions for issue creation, field/status/sprint/comment changes, labels, assignment, comments, transitions, and notifications.

### 26. Recurring issues

**Chosen:** Not implemented initially

Users create repeating work manually; no recurring issue schedules or templates.

### 27. Priorities

**Chosen:** Fixed global set

Highest, High, Medium, Low, Lowest. Administrators cannot add, rename, reorder, or scope priorities initially.

### 28. Resolutions

**Chosen:** Fixed global set

Fixed, Done, Won’t Fix, Duplicate, Cannot Reproduce. Resolution is separate from workflow status.

### 29. Issue types

**Chosen:** Fixed initial set

Epic, Story, Task, Bug, Sub-task. No custom issue types or issue-type schemes initially.

### 30. Sprints

**Chosen:** Jira-like sprints with one active sprint per Scrum board

Support goal, dates, planned/active/completed states, capacity by points/time, backlog movement, closing behavior, sprint report, and scope-change history. No parallel active sprints.

### 31. Manual ordering

**Chosen:** Global Jira-like rank

Use a LexoRank-like sortable value so drag-and-drop can insert between neighbors without renumbering the whole backlog. The same rank drives backlog, Scrum, and Kanban ordering.

### 32. Board columns

**Chosen:** One board column equals one workflow status

Moving an issue to a column transitions it to that exact status. Multiple statuses in one column are not supported initially.

### 33. Kanban WIP limits

**Chosen:** Soft warning limits

Exceeding a column limit is visually highlighted but does not block moving more issues into the column.

### 34. Swimlanes

**Chosen:** Configurable by Epic, assignee, project, priority, or issue type

Board users/administrators can select one of these grouping dimensions.

### 35. Quick filters

**Chosen:** Configurable visual quick filters

Board administrators create reusable buttons using the same structured form-filter conditions; no JQL.

### 36. Bulk operations

**Chosen:** Jira-like multi-step wizard

Select issues, choose operation, configure values, review, confirm. Support field updates, transitions, compatible project moves, issue-type changes, and moving to recycle bin with permission/required-field validation.

### 37. Moving issues between projects

**Chosen:** Only between compatible projects

Source and target must have compatible issue type, status/workflow, required-field, and custom-field configurations. Otherwise block the move and explain why; do not implement a full mapping wizard initially.

### 38. Old issue keys after move

**Chosen:** Permanent aliases

Old keys always redirect to the issue’s current key and can never be reused for another issue.

### 39. Integration API

**Chosen:** Public REST API, PATs, service accounts, and webhooks

Expose a versioned API for external tools. Include scoped personal tokens, technical service accounts, and outbound event webhooks.

### 40. Token security

**Chosen:** Scopes, expiration, revocation, rotation, and audit

Track last use and administrator maximum lifetime. A token never exceeds the permissions of its owner/service account.

### 41. Webhook filtering

**Chosen:** By project and structured visual filter

Webhook subscriptions can filter issue type, status, priority, assignee, standard fields, custom fields, and events. No JQL or arbitrary payload-transform scripts initially.

### 42. Git integration

**Chosen:** Lightweight issue-key linking

External integrations may attach commit, branch, pull/merge request, and build links by recognizing keys such as CNA-123. No full repository account integration or automatic workflow transitions initially.

### 43. Full-text search

**Chosen:** Native implementation per database

PostgreSQL uses tsvector/tsquery/GIN. SQLite uses FTS5. Both are hidden behind a shared search interface.

### 44. Internationalization

**Chosen:** General i18n framework

English is the default; Czech is a complete included translation. Additional languages are added through resource files without C++ changes. Backend messages and email templates are localized too.

### 45. Time zones and dates

**Chosen:** UTC storage plus per-user time zone and locale formatting

Auto-detect browser zone, permit profile override, support 12/24-hour preference and DST. Date-only values remain date-only so zones cannot shift the calendar day.

### 46. Themes and branding

**Chosen:** Light, dark, high contrast, and limited installation branding

Admins may set installation name, logo, favicon, accent color, and login branding. No arbitrary custom CSS/plugin themes.

### 47. Accessibility

**Chosen:** Target WCAG 2.2 AA

Semantic HTML, full keyboard support, visible focus, screen-reader support, accessible dialogs, non-drag alternatives, contrast, and automated/manual testing.

### 48. Import/export

**Chosen:** CSV, complete app backup/restore, and Jira migration tool

Jira migration covers projects, keys, users, comments, attachments, statuses, priorities, sprints, Epics, versions, and available history, with a report for unsupported/lossy mappings.

### 49. Extensions

**Chosen:** Safe external-app model

Third-party extensions run outside the Ticket Hub process and integrate through REST, webhooks, service accounts, declared UI links/panels, and scoped permissions. Built-in optional features may be C++ modules; no untrusted dynamic C++ plugins.

### 50. Distribution/deployment

**Chosen:** Native packages, Docker, Compose, Kubernetes/Helm

Provide native binaries, .deb, .rpm, official Docker image, Compose with PostgreSQL, Helm chart, readiness/liveness, and horizontal app scaling with PostgreSQL plus shared S3.

### 51. Background jobs

**Chosen:** Pluggable durable queue

Default queue is database-backed: PostgreSQL supports multiple workers/nodes; SQLite uses a simpler single-instance worker. Leave room for Redis later.

### 52. Outbound email

**Chosen:** Pluggable delivery backend

Support SMTP, local sendmail, and provider APIs behind one interface. Prioritize SMTP/sendmail first.

### 53. Password security

**Chosen:** Argon2id and hardened local authentication

Include strength checks, login rate limits, temporary lockout, email reset, and invalidation of all sessions after password change. No MFA/WebAuthn initially.

### 54. Web sessions versus API authentication

**Chosen:** Server-side cookie sessions for web; tokens for API

Session IDs are random and stored in HttpOnly/Secure/SameSite cookies with expiry and active-session management. API clients use PAT/service tokens.

### 55. OIDC provisioning

**Chosen:** Configurable per provider

Choose automatic account creation or pre-existing only; restrict domains; assign default groups; map external groups. No periodic external-directory deprovisioning initially.

### 56. User identity fields

**Chosen:** Immutable UUID, unique email, optional unique handle

Display name need not be unique. Internal references always use UUID so email/handle changes do not break history.

### 57. User departure/duplicates

**Chosen:** Deactivate, anonymize, and merge

Preserve issue history and transfer ownership/references to a surviving account when merging duplicates or local/OIDC identities.

### 58. Project visibility

**Chosen:** Permission schemes only

No separate Private/Internal/Public flag. Visibility grants may target roles, groups, all authenticated users, or anonymous users if globally enabled.

### 59. Anonymous access

**Chosen:** Optional read-only, disabled by default

When globally enabled and granted by permission scheme, anonymous users may browse projects/issues and explicitly public attachments. They cannot create/comment/modify.

### 60. Issue cloning

**Chosen:** Jira-like selectable cloning, including compatible target project

Options include attachments, links, sub-tasks, sprint, Epic, assignee, versions, and custom fields. Create a clones/is cloned by link.

### 61. Issue templates

**Chosen:** Global and project templates with optional generated structure

Templates prefill fields and may create sub-tasks, issue links, Markdown checklists, and default watchers.

### 62. Checklists

**Chosen:** Markdown checklists only

No separate checklist entities, assignees, due dates, history, or conversion feature initially.

### 63. Project ownership of an issue

**Chosen:** Exactly one project

An issue always has one project and one current project-specific key. Boards/filters may span projects; issues are never multi-project or projectless.

### 64. Sub-task model

**Chosen:** Simple Jira-like sub-tasks

A Sub-task must have a Story/Task/Bug parent, cannot exist independently, cannot have children, and remains in the same project as its parent.

### 65. Epic membership

**Chosen:** Optional, at most one Epic

Story, Task, or Bug may belong to zero or one Epic. Sub-tasks inherit Epic context through their parent.

### 66. Backlog issues without Epic

**Chosen:** Dedicated No Epic group

Unassigned issues remain visible and can be moved into/out of an Epic.

### 67. Sub-task sprint assignment

**Chosen:** Inherit parent sprint

Sub-tasks cannot be scheduled into a different sprint and appear nested under the parent in backlog/sprint views.

### 68. Completing parent with unfinished sub-tasks

**Chosen:** Controlled by workflow validator

Some transitions may require all sub-tasks complete; others may allow completion/cancellation.

### 69. Reopening parent and sub-tasks

**Chosen:** Controlled by workflow post-function

A transition may leave sub-tasks, reopen all, or reopen those in selected statuses.

### 70. Resolution on transition/reopen

**Chosen:** Controlled by workflow post-function

A transition may clear, preserve, or set a fixed resolution. Reopening clears resolution by default.

### 71. Status categories

**Chosen:** Exactly To Do, In Progress, Done

Every custom status belongs to one category. Administrators cannot create more categories.

### 72. Retiring a used status

**Chosen:** Migration wizard

Choose replacement status per affected workflow, review impacted issues, confirm migration, then archive/remove, with a complete audit trail.

### 73. Workflow editing/publishing

**Chosen:** Editable draft, one active published version, version history

Publishing includes impact review and needed status migration. Prior versions remain for audit and controlled rollback; projects do not keep many active versions indefinitely.

### 74. Transition conditions

**Chosen:** Safe Jira-like configurable conditions

Conditions may depend on roles, groups, users, assignee, reporter, project lead, field values, prior status, and AND/OR combinations. No custom scripts.

### 75. Transition validators

**Chosen:** Safe configurable Jira-like validators

Support required fields, sub-task completion, assignee/Fix Version, numeric/date rules, permissions, issue type/field conditions, attachment/comment counts, links, worklogs, and related-issue statuses. No custom scripts.

### 76. Transition post-functions

**Chosen:** Ordered configurable chain plus mandatory internal steps

Actions include field/resolution/assignee changes, comments, labels, notifications, estimate updates, and sub-task actions. Core state/history steps cannot be removed. No custom scripts.

### 77. Transition forms

**Chosen:** Simple per-transition field list

Administrators choose fields, order, required flags, defaults, comment field, and help text. No shared Jira screen scheme.

### 78. SLA

**Chosen:** Not implemented

Use due dates, estimates, worklogs, and agile reports only. No SLA calendars/timers/reports.

### 79. Issue voting

**Chosen:** One Jira-like vote per authenticated user

Display vote count and voters. Votes do not automatically alter priority.

### 80. Mentions

**Chosen:** User mentions in all Markdown-capable fields

Descriptions, comments, Markdown custom fields, release notes, etc. notify referenced users. No group/role mentions initially.

### 81. Edited comment history

**Chosen:** Keep all immutable versions

Record author and timestamp of each edit; authorized users can view previous versions. No short edit window.

### 82. Deleted comments

**Chosen:** Tombstone for users; content retained in admin audit

Normal discussion shows who deleted and when. Original content/version history is restricted to authorized administrators.

### 83. Comment permissions

**Chosen:** Separate Jira-like permissions

Edit own, edit all, delete own, delete all.

### 84. Emoji reactions

**Chosen:** Fixed reactions on comments only

Each user can add each reaction once per comment. Issues use the separate voting feature.

### 85. Comment threading

**Chosen:** No threads

Use one chronological discussion with mentions, citations, and links to specific comments.

### 86. Personal notification preferences

**Chosen:** Per-event/channel controls plus digests

Support immediate and daily/other supported summaries for non-mandatory events. Security and critical administrative notifications cannot be disabled.

### 87. Project archival

**Chosen:** Read-only archive

Archived projects leave active lists, remain viewable by authorized users, cannot be modified, and can be restored.

### 88. Permanent project deletion

**Chosen:** Require complete export, then recycle bin

Only after verified export may the project enter the bin; admins can restore or permanently delete.

### 89. Project recycle-bin retention

**Chosen:** Configurable; default 90 days

Allow 30/90/365 days or never auto-delete. Permanent deletion is audited and requires the export.

### 90. Project key while project is in bin

**Chosen:** Reserved

The key cannot be reused until permanent project deletion, preserving restoration and links.

### 91. Changing active project key

**Chosen:** Allowed with permanent old issue-key aliases

Issue numeric suffixes remain; old project/issue keys continue redirecting and remain reserved.

### 92. Issue number reuse

**Chosen:** Never reuse

Even permanent deletion does not free the number. Old references can never resolve to a different issue.

### 93. Project key syntax

**Chosen:** Uppercase letters/digits, starts with letter, length 2–12

Canonical regex: ^[A-Z][A-Z0-9]{1,11}$. Examples CNA, CNA2, DX12.

### 94. Case handling for keys

**Chosen:** Case-insensitive input, uppercase canonical form

URLs, API, search, imports, Git recognition, and aliases accept mixed/lowercase and normalize to uppercase.

### 95. Initial issue number

**Chosen:** Administrator-selectable at project creation/import; default 1

After issue creation begins the sequence can never be lowered or reused.

### 96. Gaps in issue numbering

**Chosen:** Allowed

Failed/rolled-back creation may consume a number. Stability and uniqueness matter, not a contiguous accounting sequence.

### 97. Labels

**Chosen:** Free-form with autocomplete and normalization

Issues may have many labels. Compare case-insensitively and store one canonical normalized form.

### 98. Attachment limits/security

**Chosen:** Configurable size/count/MIME/extension/quota limits

No antivirus or DLP initially. Validate declared MIME and basic content type where feasible.

### 99. Attachment previews

**Chosen:** Safe common in-browser previews

Images, PDF, text/source, and browser-supported audio/video. Other formats are download-only. No Office conversion/external preview service.

### 100. Inline images in Markdown

**Chosen:** Ordinary attachments referenced by internal UUID URL

Support upload, drag/drop, and clipboard paste. Use attachment://UUID or equivalent. No unrestricted external image embedding initially.

### 101. Attachment listing/deletion

**Chosen:** Sortable list plus recycle bin

Sort by name, size, date, author, type. Deletion makes attachment unavailable and Markdown shows a removed marker; authorized admins may restore or permanently delete.

### 102. Attachment recycle-bin retention

**Chosen:** Configurable; default 90 days

Allow disabling automatic purge. Before purge, verify no valid reference still uses the object.

### 103. Duplicate attachment filename

**Chosen:** Create independent immutable attachment

Same filename is allowed. Each upload has its own UUID, author, timestamp, and storage object; old links never change.

### 104. Physical attachment deduplication

**Chosen:** None initially

Every upload stores an independent physical object even if contents are identical. SHA-256 is for integrity, not shared storage.

### 105. Attachment integrity

**Chosen:** Verify at upload and via periodic background audits

Check SHA-256, size, and object presence; report missing/altered/corrupt objects. Do not hash every download by default.

### 106. Backup layout for attachments

**Chosen:** Database/config and attachment objects separate, joined by manifest

Use checksums and one backup identity for scalable consistent restore.

### 107. Backup consistency mode

**Chosen:** Both offline/read-only and online snapshot

PostgreSQL production defaults to online consistent snapshot; SQLite may use a short read-only maintenance window.

### 108. Restore safety

**Chosen:** Restore into temporary isolated environment, verify, then switch atomically

Validate manifest, checksums, schema, data integrity, then briefly stop writes and switch. Keep old data temporarily for emergency rollback.

### 109. Backup version direction

**Chosen:** Forward migrate older supported backups; reject newer backups in older app

No database downgrade support.

### 110. Very old backup compatibility

**Chosen:** Documented LTS migration checkpoints

Direct support is bounded; ancient backups move through bridge releases such as 1.x → 3.x LTS → 5.x LTS → current.

### 111. Application upgrades

**Chosen:** Staged check/apply plus rolling upgrades where possible

Preflight checks DB/schema, space, storage, jobs, backup, migration path, long/blocking work. PostgreSQL/Kubernetes may roll when schema is dual-compatible; SQLite uses maintenance.

### 112. Automatic application updates

**Chosen:** Notify only

Ticket Hub checks and reports available versions/release notes/security alerts, but never downloads or installs automatically.

### 113. Release channels

**Chosen:** Stable, LTS, Preview

Stable default; LTS conservative/long-supported; Preview clearly non-production.

### 114. Changing database backend

**Chosen:** Database-neutral application export/import

Support SQLite↔PostgreSQL where target validation succeeds. Rebuild backend-specific full-text indexes. Reject unsafe PostgreSQL→SQLite downsizing.

### 115. Inbound email features

**Chosen:** Create issues and turn replies into comments

Map subject/body/attachments, securely identify target issue, map sender, prevent spoofing and loops. No fully general Jira mail-handler rule engine.

### 116. Unknown inbound email sender

**Chosen:** Always reject

Only an existing active user may create/comment by email. Do not auto-create or queue unknown senders.

### 117. Reply trimming

**Chosen:** Reply-above-marker with conservative fallback

Store content above a fixed Ticket Hub marker. If missing, cautiously strip quoted history/common separators; do not aggressively destroy signatures/content.

### 118. Inbound email body format

**Chosen:** Prefer usable text/plain; sanitized HTML-to-Markdown fallback

Remove scripts, styles, tracking, and unsafe remote resources.

### 119. Duplicate filenames in inbound email

**Chosen:** Store all as separate immutable attachments

Filenames are not unique; UUIDs identify objects.

### 120. Inbound mail transport

**Chosen:** Pluggable IMAP, mail pipe, and HTTP webhook adapters

Prioritize IMAP and local pipe first; provider webhooks later.

### 121. IMAP delivery mode

**Chosen:** IMAP IDLE with configurable polling fallback

Reconnect on failure and expose connection/last-success status.

### 122. Inbound email idempotency

**Chosen:** Multiple identifiers plus quarantine

Use Message-ID, provider/IMAP UID, normalized content checksum, and processing states. Suspicious conflicts go to an admin quarantine.

### 123. Inbound email processing failure

**Chosen:** Retry with backoff, then dead-letter queue; atomic processing

Admins inspect reason/attempts/metadata and can retry. Either complete issue/comment with accepted attachments commits or nothing does.

### 124. REST rate limiting

**Chosen:** Configurable multi-level limits

Limit by IP, user/token, service account, endpoint, operation type, and installation capacity. Return HTTP 429 and Retry-After.

### 125. REST request and batch limits

**Chosen:** Endpoint-specific body/item limits with audited service-account exceptions

Cover JSON, Markdown, imports, attachments, bulk size, filter complexity, recipients, webhook events, and page size.

### 126. REST pagination

**Chosen:** Cursor-based and numbered/offset modes

Public API prefers stable cursor pagination; UI and selected admin endpoints may use numbered pages/offset.

### 127. REST API versioning

**Chosen:** Major version in URL and backward-compatible evolution inside major

Use /api/v1. Add optional fields/endpoints compatibly; breaking changes require /api/v2 with support/deprecation period.

### 128. REST write idempotency keys

**Chosen:** Required for high-risk operations

Require Idempotency-Key for issue/comment/worklog creation, bulk changes, imports, and other duplicate-side-effect risks. Simpler updates may accept it optionally.

### 129. Concurrent issue editing

**Chosen:** Optimistic locking with conflict comparison

Issues carry a version. Stale writes fail (typically 409); UI shows other changes vs local edits and allows reload/reapply.

### 130. Realtime browser updates

**Chosen:** Server-Sent Events with polling fallback

Cover issue/comment/board/sprint/notification updates. Multi-node mode distributes events across app instances.

### 131. Internal event distribution

**Chosen:** Pluggable; durable DB event log plus PostgreSQL LISTEN/NOTIFY initially

NOTIFY wakes nodes, which load durable missing events. SQLite uses local event loop. Allow Redis Streams/broker later.

### 132. Caching

**Chosen:** Pluggable; per-instance in-memory by default

Invalidate via event bus. Critical/fast-changing data bypass cache. Database remains source of truth; Redis optional later.

### 133. Observability

**Chosen:** Pluggable JSON logs, Prometheus metrics, OpenTelemetry, correlation IDs

Exporters can be enabled/disabled/replaced for native, Docker, and Kubernetes.

### 134. Secrets management

**Chosen:** Pluggable backend

Support environment variables, Docker/Kubernetes secrets, restricted files, encrypted DB values, and future Vault. Master encryption key must stay outside the encrypted-secrets database.

### 135. Encryption at rest

**Chosen:** Pluggable policy

Infrastructure encryption is default. Optional application encryption for attachments and selected sensitive custom fields uses external keys; encrypted fields may lose search/sort/filter features.

### 136. Issue recycle-bin retention

**Chosen:** Configurable; default 90 days

Allow 30/90/365 days or no automatic permanent deletion.

### 137. SQLite product scope

**Chosen:** Same user-facing features with operational limits

One Ticket Hub server process, limited worker concurrency, no horizontal scaling, and documented lower recommended sizes. Do not create a deliberately feature-cut edition.

### 138. Server platforms

**Chosen:** Linux fully supported; Windows/macOS best effort

Official Linux packages/container/Kubernetes and operational tests. Other platforms should build/run where possible but lack equal production support.

### 139. Browser support

**Chosen:** Latest two major Chrome, Firefox, Edge, Safari; progressive degradation

Older modern browsers may support basic read-only use; advanced boards, drag/drop, SSE, and administration are not guaranteed. No Internet Explorer.

### 140. Frontend architecture

**Chosen:** Hybrid real HTML plus vanilla JS progressive enhancement

Crow serves direct URLs/HTML. Vanilla JS enhances navigation, boards, dialogs, filters, forms, REST, SSE. No React/Vue/Angular and no initial offline PWA.

### 141. License

**Chosen:** MIT License

Commercial/noncommercial use, modification, distribution, proprietary integration, and hosted closed variants are allowed as long as the MIT notice/license is retained.

## Closed scope

The interactive product-definition pass is complete. Implementation should not reopen minor product questions. Choose conservative defaults consistent with these decisions. Create an ADR only when implementation reveals a real contradiction, security problem, or unavoidable architectural tradeoff.