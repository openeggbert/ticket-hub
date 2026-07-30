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

- Decisions completed: 10 / 142
- Decisions remaining: 132
- KEEP_FOR_V1: 2
- SIMPLIFY_FOR_V1: 4
- DEFER_AFTER_V1: 1
- REMOVE_COMPLETELY: 3
- ARCHITECTURE_ONLY: 0
- ALREADY_IMPLEMENTED_AND_KEEP: 0
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
| 10 | Issue searching |  |  |  |  |  | PENDING |
| 11 | Agile reports |  |  |  |  |  | PENDING |
| 12 | Estimation |  |  |  |  |  | PENDING |
| 13 | Worklogs |  |  |  |  |  | PENDING |
| 14 | Notifications |  |  |  |  |  | PENDING |
| 15 | Attachment storage |  |  |  |  |  | PENDING |
| 16 | Rich text |  |  |  |  |  | PENDING |
| 17 | Issue links |  |  |  |  |  | PENDING |
| 18 | Versions and releases |  |  |  |  |  | PENDING |
| 19 | Components |  |  |  |  |  | PENDING |
| 20 | Watchers |  |  |  |  |  | PENDING |
| 21 | Issue-level security |  |  |  |  |  | PENDING |
| 22 | Issue deletion |  |  |  |  |  | PENDING |
| 23 | Audit |  |  |  |  |  | PENDING |
| 24 | Dashboards |  |  |  |  |  | PENDING |
| 25 | Automation |  |  |  |  |  | PENDING |
| 26 | Recurring issues |  |  |  |  |  | PENDING |
| 27 | Priorities |  |  |  |  |  | PENDING |
| 28 | Resolutions |  |  |  |  |  | PENDING |
| 29 | Issue types |  |  |  |  |  | PENDING |
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
