# Scope Reduction Progress

This file is the authoritative, persistent record of the interactive scope-reduction questionnaire being run against `docs/PRODUCT_DECISIONS_COMPLETE.md`. It is updated after every single answered decision so that another Claude Code session can resume without any conversational memory.

## How to resume this questionnaire in a fresh session

1. Read this file top to bottom.
2. Find the first row with `Status = PENDING`.
3. Continue asking decisions in order starting there, one question at a time, following the rules in the original scope-reduction task prompt (see `handoff/NEXT_SESSION_PROMPT.txt` history and this project's git log for the original instructions).
4. Never mark a decision `DONE` without the user's explicit answer in this session.

## Classification legend

- `KEEP_FOR_V1` — implement as originally decided.
- `SIMPLIFY_FOR_V1` — implement a cheaper version in V1.
- `DEFER_AFTER_V1` — real feature, not in V1, revisit later.
- `REMOVE_COMPLETELY` — not part of Ticket Hub's plan at all anymore.
- `ARCHITECTURE_ONLY` — leave an extension point, do not implement the feature.
- `ALREADY_IMPLEMENTED_AND_KEEP` — exists in the current prototype and is kept as is.

## Summary counters (updated after every 10 decisions)

- Decisions completed: 30 / 142
- Decisions remaining: 112
- KEEP_FOR_V1: 4
- SIMPLIFY_FOR_V1: 13
- DEFER_AFTER_V1: 4
- REMOVE_COMPLETELY: 5
- ARCHITECTURE_ONLY: 0
- ALREADY_IMPLEMENTED_AND_KEEP: 4
- Cumulative estimated hours saved: see SCOPE_REDUCTION_ESTIMATE.md running totals


## Decision table

| # | Topic | Original Decision (summary) | New Answer | Final Classification | Reason | Est. Hours Saved | Status |
|---|---|---|---|---|---|---|---|
| 0 | Installation/organization model | Single-tenant self-hosted instance, no multi-tenant org table | Confirmed single-tenant; no extension point reserved | KEEP_FOR_V1 | Already the cheapest option; user chose to keep as-is without reserving a future multi-tenant hook | 0-2h | DONE |
| 1 | Authentication providers | Local accounts plus OpenID Connect (multi-provider, JIT provisioning, group mapping) | Local accounts only; OIDC removed entirely from V1 (not even an architecture stub) | REMOVE_COMPLETELY | OIDC discovery/JWKS/account-linking/per-provider config is one of the largest Auth-phase cost centers; user chose local-only for smallest V1 | 15-30h | DONE |
| 2 | Registration modes | Configurable per installation (public / invitation-only / admin-created); invitation-only default; OIDC JIT separately configurable | Admin-created accounts only; no public registration, no invitation flow; OIDC JIT provisioning moot (OIDC removed in Decision 1) | SIMPLIFY_FOR_V1 | Single fixed registration mode removes invitation-token flow and public-registration abuse protections while still using the same users table | 5-10h | DONE |
| 3 | Project authorization | Jira-like configurable permission schemes assigned to projects; grants target users/groups/roles/reporter/assignee/lead/anon | Fixed small set of project roles (e.g. Admin/Member/Viewer); no configurable schemes, no reporter/assignee/anon grant targets | SIMPLIFY_FOR_V1 | Configurable permission schemes are one of the most complex Jira subsystems; fixed roles cover the vast majority of small-team needs | 30-50h | DONE |
| 4 | Workflow model | Shared Jira-like workflow schemes: statuses, transitions, conditions, validators, ordered post-functions, draft/publish | One fixed built-in workflow for all projects; no designer, no conditions/validators/post-functions, no draft/publish. Predetermines simplification of Decisions 71-77. | SIMPLIFY_FOR_V1 | Configurable workflow engine is the single most expensive subsystem in the original scope; fixed workflow covers typical small-team usage | 60-100h | DONE |
| 5 | Issue hierarchy direction | Jira-like base hierarchy Epic->issue->Sub-task, with documented future extensibility above Epic | Drop the future-extensibility-above-Epic note entirely; hierarchy is fixed at Epic -> Story/Task/Bug -> Sub-task only | REMOVE_COMPLETELY | Documentation-only aspiration with no implementation cost either way; user chose not to carry the speculative note forward | 0h | DONE |
| 6 | Project types | Software projects only; Business/Service Management excluded | Confirmed unchanged | KEEP_FOR_V1 | Already minimal scope, nothing to reduce | 0h | DONE |
| 7 | Board types and scope | Scrum and Kanban; multiple boards per project; saved-filter based; multi-project boards | Kanban only, one auto board per project, no saved-filter/multi-project boards. Predetermines simplification of Decision 30 (Scrum sprints removed). | SIMPLIFY_FOR_V1 | Scrum brings the full sprint/backlog/velocity model; Kanban alone covers most small-team needs at a fraction of the cost | 40-60h | DONE |
| 8 | Project management style | Company-managed first; team-managed later; architecture should not block it | Drop the team-managed future note entirely; distinction is moot now that roles/workflow are fixed rather than schemed | REMOVE_COMPLETELY | With fixed roles (Decision 3) and fixed workflow (Decision 4), there is nothing left to distinguish company-managed from team-managed; the aspiration note is removed | 0h | DONE |
| 9 | Custom fields and screens | Custom fields with project/issue-type contexts, ordering, required/hidden, show-on-create/edit/view; no screen schemes | No custom fields in V1 at all; only the fixed standard issue field set | DEFER_AFTER_V1 | Custom fields require a full dynamic field-type/context/form-rendering system even without screen schemes; deferred rather than built for a small fixed field set | 30-50h | DONE |
| 10 | Issue searching | Visual/form-based saved+shared filters as source for boards/quick filters/webhooks; no JQL | Ad-hoc in-UI filters only (project/type/status/priority/assignee/labels/date); no saving, no sharing, not usable as webhook/board source | SIMPLIFY_FOR_V1 | JQL already excluded originally; saved/shared filter persistence and sharing model dropped as the next-most-expensive layer | 15-25h | DONE |
| 11 | Agile reports | Burndown, sprint report, velocity, cumulative flow, control chart, release burndown | No agile reports in V1 at all, including CFD/control chart; burndown/sprint report/velocity already moot after Scrum removal (Decision 7) | DEFER_AFTER_V1 | Burndown/sprint-report/velocity require sprints which were removed; user chose to defer even Kanban-compatible reports rather than build a partial report layer | 25-40h | DONE |
| 12 | Estimation | Story points and time estimates (original/remaining/logged) | Story points only (already exists in prototype); no time estimates, those are folded into the Decision 13 worklog simplification | ALREADY_IMPLEMENTED_AND_KEEP | Story points field already exists; time estimate tracking dropped alongside worklog simplification | 3-6h | DONE |
| 13 | Worklogs | Jira-like worklogs: time spent, work date, comment, remaining-estimate adjustment, own vs others permissions | Simple worklogs: time spent + comment, no remaining-estimate linkage, no separate own-vs-others edit/delete permission split (anyone with issue access can edit) | SIMPLIFY_FOR_V1 | Time-estimate linkage removed since Decision 12 dropped time estimates; permission split dropped as a smaller simplification the user chose over full removal | 10-18h | DONE |
| 14 | Notifications | Jira-like notification schemes mapping events to assignee/reporter/watchers/roles/groups/users; in-app+email; per-user preferences and digests | Fixed in-app notifications only (assigned-to-me, mentioned, comment on watched issue); no schemes, no email, no per-user preferences/digests | SIMPLIFY_FOR_V1 | Configurable notification schemes are a full admin subsystem like permission schemes; email delivery also requires SMTP/queue infra deferred with Decision 52 | 30-50h | DONE |
| 15 | Attachment storage | Pluggable attachment storage; initial backends local filesystem + S3-compatible object storage; DB stores metadata/object key only | Local filesystem only, hardwired (no abstract storage port/interface); S3 not built and no extension point reserved for it in V1 | SIMPLIFY_FOR_V1 | User explicitly chose the cheaper hardwired option over reserving a storage port, prioritizing minimum V1 cost over later S3 extensibility | 15-25h | DONE |
| 16 | Rich text | Markdown storage with visual toolbar, live preview, attachment/mention integration in editor; sanitized HTML rendering | Keep full visual toolbar + preview editor as originally planned | KEEP_FOR_V1 | User chose to preserve editor UX quality despite the extra cost; sanitization was mandatory regardless of scope | 0h (kept as-is) | DONE |
| 17 | Issue links | Configurable bidirectional Jira-like link types defined by admins (outward/inward labels) | Fixed larger built-in set: blocks/is blocked by, relates to, duplicates/is duplicated by, clones/is cloned by; no admin editing | SIMPLIFY_FOR_V1 | Fixed catalog covers common link semantics without an admin CRUD subsystem for link-type management | 10-15h | DONE |
| 18 | Versions and releases | Jira-like project versions/releases: multiple Fix/Affects Version, release notes, progress, release/archive/merge | No versions/releases concept in V1 at all; labels can informally track release if needed | DEFER_AFTER_V1 | Full version subsystem is mid-cost on its own and release burndown report already dropped in Decision 11, reducing its remaining value for V1 | 20-30h | DONE |
| 19 | Components | Simple project components: name, description, lead, default assignee; at most one per issue | Keep as originally decided | KEEP_FOR_V1 | Already the cheapest reasonable form; small table plus one optional issue field | 0h (kept as-is) | DONE |
| 20 | Watchers | Jira-like watchers: self watch/unwatch plus authorized users managing others watchers | Self watch/unwatch only; no managing other users watchers | SIMPLIFY_FOR_V1 | Removes the extra authorization check for managing other users watch state; simple many-to-many table remains | 2-4h | DONE |
| 21 | Issue-level security | Not implemented initially; Browse Project sees all live issues | Confirmed unchanged | REMOVE_COMPLETELY | Was already never planned for implementation; correcting classification from an earlier KEEP_FOR_V1 mislabel (feature absent, not kept) | 0h | DONE |
| 22 | Issue deletion | Recycle bin / soft deletion; restore or permanent delete by authorized admins; audited | Keep recycle bin as originally decided; build the UI on top of the existing soft-delete columns | ALREADY_IMPLEMENTED_AND_KEEP | Soft-delete schema/query filtering already exists in the prototype; only recycle-bin UI (list/restore/purge) remains to add | 0h (kept as-is) | DONE |
| 23 | Audit | Global admin audit (config/user/security/workflow/project changes, auth events) plus issue history, export, category-based retention | Simple append-only audit log for admin/security events plus existing issue history; no categories, export, or configurable retention | SIMPLIFY_FOR_V1 | Issue history already partly exists; admin scope itself shrank a lot after removing schemes/workflow designer, reducing what needs auditing | 10-15h | DONE |
| 24 | Dashboards | One fixed personal dashboard: assigned issues, watched issues, recent activity, active sprint, deadlines, simple stats | Keep fixed dashboard; drop the active-sprint widget since Scrum was removed in Decision 7 | SIMPLIFY_FOR_V1 | Already the cheapest option; only change is dropping the now-moot active-sprint widget | 1-2h | DONE |
| 25 | Automation | Simple built-in automation rules: triggers, structured conditions, fixed actions | No automation in V1 at all; all actions performed manually | DEFER_AFTER_V1 | Even simple automation requires a rule model, builder UI, safe executor and loop protection; small standalone rule engine deferred entirely | 25-40h | DONE |
| 26 | Recurring issues | Not implemented initially; manual repeating work only | Confirmed unchanged | REMOVE_COMPLETELY | Was already never planned for implementation; consistent with automation removal in Decision 25 | 0h | DONE |
| 27 | Priorities | Fixed global priority set: Highest/High/Medium/Low/Lowest, no admin config | Confirmed unchanged | ALREADY_IMPLEMENTED_AND_KEEP | Already implemented in prototype seed data; already minimal scope | 0h | DONE |
| 28 | Resolutions | Fixed global resolution set: Fixed/Done/Wont Fix/Duplicate/Cannot Reproduce, separate from status | Keep the 5 fixed values; set manually as a plain field on transition to Done, no post-function logic (consistent with fixed workflow from Decision 4) | SIMPLIFY_FOR_V1 | Value set already minimal; setting mechanism simplified to match the fixed workflow with no post-functions | 2-4h | DONE |
| 29 | Issue types | Fixed initial issue types: Epic, Story, Task, Bug, Sub-task; no custom types/schemes | Confirmed all 5 types unchanged | ALREADY_IMPLEMENTED_AND_KEEP | Seed data already includes all 5 types; already minimal scope | 0h | DONE |
| 30 | Sprints |  |  |  |  |  | PENDING |
| 31 | Manual ordering |  |  |  |  |  | PENDING |
| 32 | Board columns |  |  |  |  |  | PENDING |
| 33 | Kanban WIP limits |  |  |  |  |  | PENDING |
| 34 | Swimlanes |  |  |  |  |  | PENDING |
| 35 | Quick filters |  |  |  |  |  | PENDING |
| 36 | Bulk operations |  |  |  |  |  | PENDING |
| 37 | Moving issues between projects |  |  |  |  |  | PENDING |
| 38 | Old issue keys after move |  |  |  |  |  | PENDING |
| 39 | Integration API |  |  |  |  |  | PENDING |
| 40 | Token security |  |  |  |  |  | PENDING |
| 41 | Webhook filtering |  |  |  |  |  | PENDING |
| 42 | Git integration |  |  |  |  |  | PENDING |
| 43 | Full-text search |  |  |  |  |  | PENDING |
| 44 | Internationalization |  |  |  |  |  | PENDING |
| 45 | Time zones and dates |  |  |  |  |  | PENDING |
| 46 | Themes and branding |  |  |  |  |  | PENDING |
| 47 | Accessibility |  |  |  |  |  | PENDING |
| 48 | Import/export |  |  |  |  |  | PENDING |
| 49 | Extensions |  |  |  |  |  | PENDING |
| 50 | Distribution/deployment |  |  |  |  |  | PENDING |
| 51 | Background jobs |  |  |  |  |  | PENDING |
| 52 | Outbound email |  |  |  |  |  | PENDING |
| 53 | Password security |  |  |  |  |  | PENDING |
| 54 | Web sessions versus API authentication |  |  |  |  |  | PENDING |
| 55 | OIDC provisioning |  |  |  |  |  | PENDING |
| 56 | User identity fields |  |  |  |  |  | PENDING |
| 57 | User departure/duplicates |  |  |  |  |  | PENDING |
| 58 | Project visibility |  |  |  |  |  | PENDING |
| 59 | Anonymous access |  |  |  |  |  | PENDING |
| 60 | Issue cloning |  |  |  |  |  | PENDING |
| 61 | Issue templates |  |  |  |  |  | PENDING |
| 62 | Checklists |  |  |  |  |  | PENDING |
| 63 | Project ownership of an issue |  |  |  |  |  | PENDING |
| 64 | Sub-task model |  |  |  |  |  | PENDING |
| 65 | Epic membership |  |  |  |  |  | PENDING |
| 66 | Backlog issues without Epic |  |  |  |  |  | PENDING |
| 67 | Sub-task sprint assignment |  |  |  |  |  | PENDING |
| 68 | Completing parent with unfinished sub-tasks |  |  |  |  |  | PENDING |
| 69 | Reopening parent and sub-tasks |  |  |  |  |  | PENDING |
| 70 | Resolution on transition/reopen |  |  |  |  |  | PENDING |
| 71 | Status categories |  |  |  |  |  | PENDING |
| 72 | Retiring a used status |  |  |  |  |  | PENDING |
| 73 | Workflow editing/publishing |  |  |  |  |  | PENDING |
| 74 | Transition conditions |  |  |  |  |  | PENDING |
| 75 | Transition validators |  |  |  |  |  | PENDING |
| 76 | Transition post-functions |  |  |  |  |  | PENDING |
| 77 | Transition forms |  |  |  |  |  | PENDING |
| 78 | SLA |  |  |  |  |  | PENDING |
| 79 | Issue voting |  |  |  |  |  | PENDING |
| 80 | Mentions |  |  |  |  |  | PENDING |
| 81 | Edited comment history |  |  |  |  |  | PENDING |
| 82 | Deleted comments |  |  |  |  |  | PENDING |
| 83 | Comment permissions |  |  |  |  |  | PENDING |
| 84 | Emoji reactions |  |  |  |  |  | PENDING |
| 85 | Comment threading |  |  |  |  |  | PENDING |
| 86 | Personal notification preferences |  |  |  |  |  | PENDING |
| 87 | Project archival |  |  |  |  |  | PENDING |
| 88 | Permanent project deletion |  |  |  |  |  | PENDING |
| 89 | Project recycle-bin retention |  |  |  |  |  | PENDING |
| 90 | Project key while project is in bin |  |  |  |  |  | PENDING |
| 91 | Changing active project key |  |  |  |  |  | PENDING |
| 92 | Issue number reuse |  |  |  |  |  | PENDING |
| 93 | Project key syntax |  |  |  |  |  | PENDING |
| 94 | Case handling for keys |  |  |  |  |  | PENDING |
| 95 | Initial issue number |  |  |  |  |  | PENDING |
| 96 | Gaps in issue numbering |  |  |  |  |  | PENDING |
| 97 | Labels |  |  |  |  |  | PENDING |
| 98 | Attachment limits/security |  |  |  |  |  | PENDING |
| 99 | Attachment previews |  |  |  |  |  | PENDING |
| 100 | Inline images in Markdown |  |  |  |  |  | PENDING |
| 101 | Attachment listing/deletion |  |  |  |  |  | PENDING |
| 102 | Attachment recycle-bin retention |  |  |  |  |  | PENDING |
| 103 | Duplicate attachment filename |  |  |  |  |  | PENDING |
| 104 | Physical attachment deduplication |  |  |  |  |  | PENDING |
| 105 | Attachment integrity |  |  |  |  |  | PENDING |
| 106 | Backup layout for attachments |  |  |  |  |  | PENDING |
| 107 | Backup consistency mode |  |  |  |  |  | PENDING |
| 108 | Restore safety |  |  |  |  |  | PENDING |
| 109 | Backup version direction |  |  |  |  |  | PENDING |
| 110 | Very old backup compatibility |  |  |  |  |  | PENDING |
| 111 | Application upgrades |  |  |  |  |  | PENDING |
| 112 | Automatic application updates |  |  |  |  |  | PENDING |
| 113 | Release channels |  |  |  |  |  | PENDING |
| 114 | Changing database backend |  |  |  |  |  | PENDING |
| 115 | Inbound email features |  |  |  |  |  | PENDING |
| 116 | Unknown inbound email sender |  |  |  |  |  | PENDING |
| 117 | Reply trimming |  |  |  |  |  | PENDING |
| 118 | Inbound email body format |  |  |  |  |  | PENDING |
| 119 | Duplicate filenames in inbound email |  |  |  |  |  | PENDING |
| 120 | Inbound mail transport |  |  |  |  |  | PENDING |
| 121 | IMAP delivery mode |  |  |  |  |  | PENDING |
| 122 | Inbound email idempotency |  |  |  |  |  | PENDING |
| 123 | Inbound email processing failure |  |  |  |  |  | PENDING |
| 124 | REST rate limiting |  |  |  |  |  | PENDING |
| 125 | REST request and batch limits |  |  |  |  |  | PENDING |
| 126 | REST pagination |  |  |  |  |  | PENDING |
| 127 | REST API versioning |  |  |  |  |  | PENDING |
| 128 | REST write idempotency keys |  |  |  |  |  | PENDING |
| 129 | Concurrent issue editing |  |  |  |  |  | PENDING |
| 130 | Realtime browser updates |  |  |  |  |  | PENDING |
| 131 | Internal event distribution |  |  |  |  |  | PENDING |
| 132 | Caching |  |  |  |  |  | PENDING |
| 133 | Observability |  |  |  |  |  | PENDING |
| 134 | Secrets management |  |  |  |  |  | PENDING |
| 135 | Encryption at rest |  |  |  |  |  | PENDING |
| 136 | Issue recycle-bin retention |  |  |  |  |  | PENDING |
| 137 | SQLite product scope |  |  |  |  |  | PENDING |
| 138 | Server platforms |  |  |  |  |  | PENDING |
| 139 | Browser support |  |  |  |  |  | PENDING |
| 140 | Frontend architecture |  |  |  |  |  | PENDING |
| 141 | License |  |  |  |  |  | PENDING |
