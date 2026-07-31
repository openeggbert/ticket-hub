# Reduced-scope decision register (V1)

Status: final V1 baseline, produced by the interactive scope-reduction questionnaire.

This is the authoritative V1 companion to `PRODUCT_DECISIONS_COMPLETE.md`. Every decision number below corresponds 1:1 to the original chronological ledger (0-141) so the two files can be read side by side. `PRODUCT_DECISIONS_COMPLETE.md` still records what the original, full-scope product-definition conversation chose; this file records what was *reaffirmed*, *simplified*, *deferred*, or *removed* during the 2026-07-31 scope-reduction pass with the product owner, and why. The full question-by-question record with cost estimates lives in `SCOPE_REDUCTION_PROGRESS.md`.

## How to read a classification

- `KEEP_FOR_V1` - build exactly as the original decision said.
- `SIMPLIFY_FOR_V1` - build a cheaper version for V1; the full original design is not implemented now.
- `DEFER_AFTER_V1` - real, wanted feature; explicitly out of V1; revisit in a later milestone.
- `REMOVE_COMPLETELY` - no longer part of the Ticket Hub plan at all, at any point.
- `ARCHITECTURE_ONLY` - an extension point may exist in the design, but the feature itself is not built in V1. (No decision landed here in this pass.)
- `ALREADY_IMPLEMENTED_AND_KEEP` - already exists in the 0.2.0 prototype in a form that satisfies V1; only incidental gaps (e.g. a missing UI) remain.

## Decisions

### 0. Installation/organization model — `KEEP_FOR_V1`

**Original (0 in PRODUCT_DECISIONS_COMPLETE.md):** Single-tenant self-hosted instance, no multi-tenant org table

**V1 answer:** Confirmed single-tenant; no extension point reserved

**Why:** Already the cheapest option; user chose to keep as-is without reserving a future multi-tenant hook

**Est. Claude Code hours saved vs. original:** 0-2h

### 1. Authentication providers — `REMOVE_COMPLETELY`

**Original (1 in PRODUCT_DECISIONS_COMPLETE.md):** Local accounts plus OpenID Connect (multi-provider, JIT provisioning, group mapping)

**V1 answer:** Local accounts only; OIDC removed entirely from V1 (not even an architecture stub)

**Why:** OIDC discovery/JWKS/account-linking/per-provider config is one of the largest Auth-phase cost centers; user chose local-only for smallest V1

**Est. Claude Code hours saved vs. original:** 15-30h

### 2. Registration modes — `SIMPLIFY_FOR_V1`

**Original (2 in PRODUCT_DECISIONS_COMPLETE.md):** Configurable per installation (public / invitation-only / admin-created); invitation-only default; OIDC JIT separately configurable

**V1 answer:** Admin-created accounts only; no public registration, no invitation flow; OIDC JIT provisioning moot (OIDC removed in Decision 1)

**Why:** Single fixed registration mode removes invitation-token flow and public-registration abuse protections while still using the same users table

**Est. Claude Code hours saved vs. original:** 5-10h

### 3. Project authorization — `SIMPLIFY_FOR_V1`

**Original (3 in PRODUCT_DECISIONS_COMPLETE.md):** Jira-like configurable permission schemes assigned to projects; grants target users/groups/roles/reporter/assignee/lead/anon

**V1 answer:** Fixed small set of project roles (e.g. Admin/Member/Viewer); no configurable schemes, no reporter/assignee/anon grant targets

**Why:** Configurable permission schemes are one of the most complex Jira subsystems; fixed roles cover the vast majority of small-team needs

**Est. Claude Code hours saved vs. original:** 30-50h

### 4. Workflow model — `SIMPLIFY_FOR_V1`

**Original (4 in PRODUCT_DECISIONS_COMPLETE.md):** Shared Jira-like workflow schemes: statuses, transitions, conditions, validators, ordered post-functions, draft/publish

**V1 answer:** One fixed built-in workflow for all projects; no designer, no conditions/validators/post-functions, no draft/publish. Predetermines simplification of Decisions 71-77.

**Why:** Configurable workflow engine is the single most expensive subsystem in the original scope; fixed workflow covers typical small-team usage

**Est. Claude Code hours saved vs. original:** 60-100h

### 5. Issue hierarchy direction — `REMOVE_COMPLETELY`

**Original (5 in PRODUCT_DECISIONS_COMPLETE.md):** Jira-like base hierarchy Epic->issue->Sub-task, with documented future extensibility above Epic

**V1 answer:** Drop the future-extensibility-above-Epic note entirely; hierarchy is fixed at Epic -> Story/Task/Bug -> Sub-task only

**Why:** Documentation-only aspiration with no implementation cost either way; user chose not to carry the speculative note forward

**Est. Claude Code hours saved vs. original:** 0h

### 6. Project types — `KEEP_FOR_V1`

**Original (6 in PRODUCT_DECISIONS_COMPLETE.md):** Software projects only; Business/Service Management excluded

**V1 answer:** Confirmed unchanged

**Why:** Already minimal scope, nothing to reduce

**Est. Claude Code hours saved vs. original:** 0h

### 7. Board types and scope — `SIMPLIFY_FOR_V1`

**Original (7 in PRODUCT_DECISIONS_COMPLETE.md):** Scrum and Kanban; multiple boards per project; saved-filter based; multi-project boards

**V1 answer:** Kanban only, one auto board per project, no saved-filter/multi-project boards. Predetermines simplification of Decision 30 (Scrum sprints removed).

**Why:** Scrum brings the full sprint/backlog/velocity model; Kanban alone covers most small-team needs at a fraction of the cost

**Est. Claude Code hours saved vs. original:** 40-60h

### 8. Project management style — `REMOVE_COMPLETELY`

**Original (8 in PRODUCT_DECISIONS_COMPLETE.md):** Company-managed first; team-managed later; architecture should not block it

**V1 answer:** Drop the team-managed future note entirely; distinction is moot now that roles/workflow are fixed rather than schemed

**Why:** With fixed roles (Decision 3) and fixed workflow (Decision 4), there is nothing left to distinguish company-managed from team-managed; the aspiration note is removed

**Est. Claude Code hours saved vs. original:** 0h

### 9. Custom fields and screens — `DEFER_AFTER_V1`

**Original (9 in PRODUCT_DECISIONS_COMPLETE.md):** Custom fields with project/issue-type contexts, ordering, required/hidden, show-on-create/edit/view; no screen schemes

**V1 answer:** No custom fields in V1 at all; only the fixed standard issue field set

**Why:** Custom fields require a full dynamic field-type/context/form-rendering system even without screen schemes; deferred rather than built for a small fixed field set

**Est. Claude Code hours saved vs. original:** 30-50h

### 10. Issue searching — `SIMPLIFY_FOR_V1`

**Original (10 in PRODUCT_DECISIONS_COMPLETE.md):** Visual/form-based saved+shared filters as source for boards/quick filters/webhooks; no JQL

**V1 answer:** Ad-hoc in-UI filters only (project/type/status/priority/assignee/labels/date); no saving, no sharing, not usable as webhook/board source

**Why:** JQL already excluded originally; saved/shared filter persistence and sharing model dropped as the next-most-expensive layer

**Est. Claude Code hours saved vs. original:** 15-25h

### 11. Agile reports — `DEFER_AFTER_V1`

**Original (11 in PRODUCT_DECISIONS_COMPLETE.md):** Burndown, sprint report, velocity, cumulative flow, control chart, release burndown

**V1 answer:** No agile reports in V1 at all, including CFD/control chart; burndown/sprint report/velocity already moot after Scrum removal (Decision 7)

**Why:** Burndown/sprint-report/velocity require sprints which were removed; user chose to defer even Kanban-compatible reports rather than build a partial report layer

**Est. Claude Code hours saved vs. original:** 25-40h

### 12. Estimation — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (12 in PRODUCT_DECISIONS_COMPLETE.md):** Story points and time estimates (original/remaining/logged)

**V1 answer:** Story points only (already exists in prototype); no time estimates, those are folded into the Decision 13 worklog simplification

**Why:** Story points field already exists; time estimate tracking dropped alongside worklog simplification

**Est. Claude Code hours saved vs. original:** 3-6h

### 13. Worklogs — `SIMPLIFY_FOR_V1`

**Original (13 in PRODUCT_DECISIONS_COMPLETE.md):** Jira-like worklogs: time spent, work date, comment, remaining-estimate adjustment, own vs others permissions

**V1 answer:** Simple worklogs: time spent + comment, no remaining-estimate linkage, no separate own-vs-others edit/delete permission split (anyone with issue access can edit)

**Why:** Time-estimate linkage removed since Decision 12 dropped time estimates; permission split dropped as a smaller simplification the user chose over full removal

**Est. Claude Code hours saved vs. original:** 10-18h

### 14. Notifications — `SIMPLIFY_FOR_V1`

**Original (14 in PRODUCT_DECISIONS_COMPLETE.md):** Jira-like notification schemes mapping events to assignee/reporter/watchers/roles/groups/users; in-app+email; per-user preferences and digests

**V1 answer:** Fixed in-app notifications only (assigned-to-me, mentioned, comment on watched issue); no schemes, no email, no per-user preferences/digests

**Why:** Configurable notification schemes are a full admin subsystem like permission schemes; email delivery also requires SMTP/queue infra deferred with Decision 52

**Est. Claude Code hours saved vs. original:** 30-50h

### 15. Attachment storage — `SIMPLIFY_FOR_V1`

**Original (15 in PRODUCT_DECISIONS_COMPLETE.md):** Pluggable attachment storage; initial backends local filesystem + S3-compatible object storage; DB stores metadata/object key only

**V1 answer:** Local filesystem only, hardwired (no abstract storage port/interface); S3 not built and no extension point reserved for it in V1

**Why:** User explicitly chose the cheaper hardwired option over reserving a storage port, prioritizing minimum V1 cost over later S3 extensibility

**Est. Claude Code hours saved vs. original:** 15-25h

### 16. Rich text — `KEEP_FOR_V1`

**Original (16 in PRODUCT_DECISIONS_COMPLETE.md):** Markdown storage with visual toolbar, live preview, attachment/mention integration in editor; sanitized HTML rendering

**V1 answer:** Keep full visual toolbar + preview editor as originally planned

**Why:** User chose to preserve editor UX quality despite the extra cost; sanitization was mandatory regardless of scope

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 17. Issue links — `SIMPLIFY_FOR_V1`

**Original (17 in PRODUCT_DECISIONS_COMPLETE.md):** Configurable bidirectional Jira-like link types defined by admins (outward/inward labels)

**V1 answer:** Fixed larger built-in set: blocks/is blocked by, relates to, duplicates/is duplicated by, clones/is cloned by; no admin editing

**Why:** Fixed catalog covers common link semantics without an admin CRUD subsystem for link-type management

**Est. Claude Code hours saved vs. original:** 10-15h

### 18. Versions and releases — `DEFER_AFTER_V1`

**Original (18 in PRODUCT_DECISIONS_COMPLETE.md):** Jira-like project versions/releases: multiple Fix/Affects Version, release notes, progress, release/archive/merge

**V1 answer:** No versions/releases concept in V1 at all; labels can informally track release if needed

**Why:** Full version subsystem is mid-cost on its own and release burndown report already dropped in Decision 11, reducing its remaining value for V1

**Est. Claude Code hours saved vs. original:** 20-30h

### 19. Components — `KEEP_FOR_V1`

**Original (19 in PRODUCT_DECISIONS_COMPLETE.md):** Simple project components: name, description, lead, default assignee; at most one per issue

**V1 answer:** Keep as originally decided

**Why:** Already the cheapest reasonable form; small table plus one optional issue field

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 20. Watchers — `SIMPLIFY_FOR_V1`

**Original (20 in PRODUCT_DECISIONS_COMPLETE.md):** Jira-like watchers: self watch/unwatch plus authorized users managing others watchers

**V1 answer:** Self watch/unwatch only; no managing other users watchers

**Why:** Removes the extra authorization check for managing other users watch state; simple many-to-many table remains

**Est. Claude Code hours saved vs. original:** 2-4h

### 21. Issue-level security — `REMOVE_COMPLETELY`

**Original (21 in PRODUCT_DECISIONS_COMPLETE.md):** Not implemented initially; Browse Project sees all live issues

**V1 answer:** Confirmed unchanged

**Why:** Was already never planned for implementation; correcting classification from an earlier KEEP_FOR_V1 mislabel (feature absent, not kept)

**Est. Claude Code hours saved vs. original:** 0h

### 22. Issue deletion — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (22 in PRODUCT_DECISIONS_COMPLETE.md):** Recycle bin / soft deletion; restore or permanent delete by authorized admins; audited

**V1 answer:** Keep recycle bin as originally decided; build the UI on top of the existing soft-delete columns

**Why:** Soft-delete schema/query filtering already exists in the prototype; only recycle-bin UI (list/restore/purge) remains to add

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 23. Audit — `SIMPLIFY_FOR_V1`

**Original (23 in PRODUCT_DECISIONS_COMPLETE.md):** Global admin audit (config/user/security/workflow/project changes, auth events) plus issue history, export, category-based retention

**V1 answer:** Simple append-only audit log for admin/security events plus existing issue history; no categories, export, or configurable retention

**Why:** Issue history already partly exists; admin scope itself shrank a lot after removing schemes/workflow designer, reducing what needs auditing

**Est. Claude Code hours saved vs. original:** 10-15h

### 24. Dashboards — `SIMPLIFY_FOR_V1`

**Original (24 in PRODUCT_DECISIONS_COMPLETE.md):** One fixed personal dashboard: assigned issues, watched issues, recent activity, active sprint, deadlines, simple stats

**V1 answer:** Keep fixed dashboard; drop the active-sprint widget since Scrum was removed in Decision 7

**Why:** Already the cheapest option; only change is dropping the now-moot active-sprint widget

**Est. Claude Code hours saved vs. original:** 1-2h

### 25. Automation — `DEFER_AFTER_V1`

**Original (25 in PRODUCT_DECISIONS_COMPLETE.md):** Simple built-in automation rules: triggers, structured conditions, fixed actions

**V1 answer:** No automation in V1 at all; all actions performed manually

**Why:** Even simple automation requires a rule model, builder UI, safe executor and loop protection; small standalone rule engine deferred entirely

**Est. Claude Code hours saved vs. original:** 25-40h

### 26. Recurring issues — `REMOVE_COMPLETELY`

**Original (26 in PRODUCT_DECISIONS_COMPLETE.md):** Not implemented initially; manual repeating work only

**V1 answer:** Confirmed unchanged

**Why:** Was already never planned for implementation; consistent with automation removal in Decision 25

**Est. Claude Code hours saved vs. original:** 0h

### 27. Priorities — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (27 in PRODUCT_DECISIONS_COMPLETE.md):** Fixed global priority set: Highest/High/Medium/Low/Lowest, no admin config

**V1 answer:** Confirmed unchanged

**Why:** Already implemented in prototype seed data; already minimal scope

**Est. Claude Code hours saved vs. original:** 0h

### 28. Resolutions — `SIMPLIFY_FOR_V1`

**Original (28 in PRODUCT_DECISIONS_COMPLETE.md):** Fixed global resolution set: Fixed/Done/Wont Fix/Duplicate/Cannot Reproduce, separate from status

**V1 answer:** Keep the 5 fixed values; set manually as a plain field on transition to Done, no post-function logic (consistent with fixed workflow from Decision 4)

**Why:** Value set already minimal; setting mechanism simplified to match the fixed workflow with no post-functions

**Est. Claude Code hours saved vs. original:** 2-4h

### 29. Issue types — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (29 in PRODUCT_DECISIONS_COMPLETE.md):** Fixed initial issue types: Epic, Story, Task, Bug, Sub-task; no custom types/schemes

**V1 answer:** Confirmed all 5 types unchanged

**Why:** Seed data already includes all 5 types; already minimal scope

**Est. Claude Code hours saved vs. original:** 0h

### 30. Sprints — `DEFER_AFTER_V1`

**Original (30 in PRODUCT_DECISIONS_COMPLETE.md):** Jira-like sprints: goal, dates, planned/active/completed states, capacity, closing behavior, scope-change history

**V1 answer:** Confirmed moot; superseded by Decision 7 (Kanban-only)

**Why:** Sprints require Scrum boards, already removed in Decision 7; savings counted there

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 7)

### 31. Manual ordering — `SIMPLIFY_FOR_V1`

**Original (31 in PRODUCT_DECISIONS_COMPLETE.md):** Global Jira-like LexoRank-style string rank driving backlog/Scrum/Kanban ordering

**V1 answer:** Simple integer order column with renumbering on insert, instead of LexoRank string ranks

**Why:** rank_value column already exists in prototype; integer renumbering is far simpler to implement/test and sufficient for small per-project issue counts

**Est. Claude Code hours saved vs. original:** 8-15h

### 32. Board columns — `KEEP_FOR_V1`

**Original (32 in PRODUCT_DECISIONS_COMPLETE.md):** One board column equals one workflow status; no multi-status columns

**V1 answer:** Confirmed unchanged; columns are simply the fixed workflow statuses from Decision 4

**Why:** Already minimal given the fixed workflow decision, no mapping configuration needed

**Est. Claude Code hours saved vs. original:** 0h

### 33. Kanban WIP limits — `KEEP_FOR_V1`

**Original (33 in PRODUCT_DECISIONS_COMPLETE.md):** Soft WIP limits per column; visually highlighted, not blocking

**V1 answer:** Keep as originally decided

**Why:** Cheap: one numeric field per column plus a display-time comparison, no blocking logic

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 34. Swimlanes — `DEFER_AFTER_V1`

**Original (34 in PRODUCT_DECISIONS_COMPLETE.md):** Configurable swimlanes by Epic/assignee/project/priority/issue type

**V1 answer:** No swimlanes in V1; board is a flat per-column list

**Why:** Group-by rendering layer is lower priority than base board functionality for a single-board small team; by-project grouping already moot after Decision 7

**Est. Claude Code hours saved vs. original:** 8-12h

### 35. Quick filters — `DEFER_AFTER_V1`

**Original (35 in PRODUCT_DECISIONS_COMPLETE.md):** Configurable quick filters using the same structured form-filter conditions as saved filters

**V1 answer:** No quick filters in V1; board uses only ad-hoc UI filtering

**Why:** Depends on saved-filter persistence which was removed in Decision 10; savings already counted there

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 10)

### 36. Bulk operations — `SIMPLIFY_FOR_V1`

**Original (36 in PRODUCT_DECISIONS_COMPLETE.md):** Jira-like multi-step bulk operation wizard: select/operation/values/review/confirm; field updates, transitions, project moves, type changes, recycle bin

**V1 answer:** Simple bulk actions: select multiple, pick one action (status/assignee/label/recycle), confirm; no multi-step wizard, no cross-project move, no type change

**Why:** Multi-step wizard across 5 operation kinds is significant UI+backend work; simple bulk actions cover most real needs

**Est. Claude Code hours saved vs. original:** 15-25h

### 37. Moving issues between projects — `SIMPLIFY_FOR_V1`

**Original (37 in PRODUCT_DECISIONS_COMPLETE.md):** Move only between compatible projects (type/workflow/field config); block and explain otherwise; no mapping wizard

**V1 answer:** Move always allowed; all projects are automatically compatible now that workflow (Decision 4) and fields (Decision 9) are fixed globally

**Why:** No compatibility check needed since every project shares the same fixed types/workflow/fields; move is just a project_id change plus new key/number

**Est. Claude Code hours saved vs. original:** 5-10h

### 38. Old issue keys after move — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (38 in PRODUCT_DECISIONS_COMPLETE.md):** Permanent aliases; old keys always redirect and never reused

**V1 answer:** Confirmed unchanged

**Why:** issue_key_aliases table and resolution already implemented in prototype; core data invariant

**Est. Claude Code hours saved vs. original:** 0h

### 39. Integration API — `SIMPLIFY_FOR_V1`

**Original (39 in PRODUCT_DECISIONS_COMPLETE.md):** Public versioned REST API, PATs, service accounts, outbound webhooks

**V1 answer:** Minimal REST API with personal access tokens (PAT) for read/write; no webhooks, no service accounts

**Why:** Full API+token+webhook subsystem is three separate subsystems; user kept basic scripted access via PAT while dropping webhooks and service accounts

**Est. Claude Code hours saved vs. original:** 25-40h

### 40. Token security — `SIMPLIFY_FOR_V1`

**Original (40 in PRODUCT_DECISIONS_COMPLETE.md):** Scopes, expiration, revocation, rotation, audit, last-used tracking, admin max lifetime; token never exceeds owner permissions

**V1 answer:** Basic token security: hashed storage, expiration, revocation, last-used tracking; no scopes (token = owner permissions), no rotation, no admin-configurable max lifetime

**Why:** Scopes/rotation are an extra layer beyond hashed-storage/expiration/revocation which are non-negotiable security basics per CLAUDE.md

**Est. Claude Code hours saved vs. original:** 5-10h

### 41. Webhook filtering — `DEFER_AFTER_V1`

**Original (41 in PRODUCT_DECISIONS_COMPLETE.md):** Webhook filtering by project and structured visual filter

**V1 answer:** Confirmed moot; superseded by Decision 39 (no webhooks in V1)

**Why:** Depends on webhooks which were removed in Decision 39; savings already counted there

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 39)

### 42. Git integration — `DEFER_AFTER_V1`

**Original (42 in PRODUCT_DECISIONS_COMPLETE.md):** Lightweight issue-key linking for commits/branches/PRs/builds via external integrations; no full repo integration

**V1 answer:** No Git integration in V1; no mechanism for external tools to attach links without public API/webhooks

**Why:** Depends on public API/webhooks removed in Decision 39; PAT alone does not provide a link-attachment mechanism

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 39)

### 43. Full-text search — `SIMPLIFY_FOR_V1`

**Original (43 in PRODUCT_DECISIONS_COMPLETE.md):** Native FTS per database: PostgreSQL tsvector/GIN, SQLite FTS5, behind shared interface

**V1 answer:** Simple LIKE/ILIKE text search on summary/description, identical across PostgreSQL and SQLite, no special indexes

**Why:** Avoids duplicate FTS implementation/index/tests per database; sufficient for small self-hosted issue volumes

**Est. Claude Code hours saved vs. original:** 15-25h

### 44. Internationalization — `DEFER_AFTER_V1`

**Original (44 in PRODUCT_DECISIONS_COMPLETE.md):** General i18n framework from the start; English default, Czech complete; resource-file extensibility; localized backend messages and email templates

**V1 answer:** English only, no i18n framework in V1; text hardcoded in UI/templates

**Why:** i18n framework doubles maintenance cost on every UI change; email templates already moot since email was removed in Decision 14

**Est. Claude Code hours saved vs. original:** 20-35h

### 45. Time zones and dates — `KEEP_FOR_V1`

**Original (45 in PRODUCT_DECISIONS_COMPLETE.md):** UTC storage plus per-user timezone: browser auto-detect, manual override, 12/24h preference, locale date format, DST correctness; date-only stays date-only

**V1 answer:** Keep as originally decided

**Why:** Basic hygiene for a usable tool; low incremental cost on top of mandatory UTC storage

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 46. Themes and branding — `SIMPLIFY_FOR_V1`

**Original (46 in PRODUCT_DECISIONS_COMPLETE.md):** Light, dark, high-contrast themes; installation branding (name/logo/favicon/accent/login)

**V1 answer:** Light and dark theme only; no high-contrast theme, no installation branding

**Why:** High-contrast theme requires careful WCAG contrast verification everywhere; branding is a deferred admin convenience feature

**Est. Claude Code hours saved vs. original:** 10-15h

### 47. Accessibility — `SIMPLIFY_FOR_V1`

**Original (47 in PRODUCT_DECISIONS_COMPLETE.md):** Target WCAG 2.2 AA: semantic HTML, keyboard, focus, screen reader, accessible dialogs, non-drag alternatives, contrast, automated+manual testing

**V1 answer:** Reasonable baseline accessibility (semantic HTML, keyboard operability) without committing to or tracking a formal WCAG level or dedicated audit deliverable

**Why:** Drops the formal WCAG compliance target and its audit/testing deliverable while keeping sane baseline practices during development

**Est. Claude Code hours saved vs. original:** 8-15h

### 48. Import/export — `REMOVE_COMPLETELY`

**Original (48 in PRODUCT_DECISIONS_COMPLETE.md):** CSV import/export, complete app backup/restore, Jira migration tool with lossy-mapping report

**V1 answer:** Simple CSV export of issues (read-only); no CSV import, no Jira migration tool; backup/restore handled separately in Decisions 106-110

**Why:** Jira migration tool would require mapping dozens of Jira entities including many already removed (sprints, versions, custom fields, permission schemes); disproportionately expensive vs value

**Est. Claude Code hours saved vs. original:** 40-70h

### 49. Extensions — `DEFER_AFTER_V1`

**Original (49 in PRODUCT_DECISIONS_COMPLETE.md):** Safe external-app model via REST/webhooks/service accounts/declared UI panels; no untrusted dynamic C++ plugins

**V1 answer:** Confirmed moot; depends on public API/webhooks/service accounts removed in Decision 39

**Why:** External extension model has no building blocks left after Decision 39 removed API/webhooks/service accounts

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 39)

### 50. Distribution/deployment — `SIMPLIFY_FOR_V1`

**Original (50 in PRODUCT_DECISIONS_COMPLETE.md):** Native binaries, .deb, .rpm, official Docker image, Compose, Helm/Kubernetes, readiness/liveness, horizontal scaling with PostgreSQL+shared S3

**V1 answer:** Docker image + Docker Compose only; no .deb/.rpm, no Helm/Kubernetes, no horizontal scaling

**Why:** One packaging path is sufficient for small self-hosted deployment; horizontal scaling already moot after S3 removal (Decision 15)

**Est. Claude Code hours saved vs. original:** 25-40h

### 51. Background jobs — `DEFER_AFTER_V1`

**Original (51 in PRODUCT_DECISIONS_COMPLETE.md):** Pluggable durable job queue; PostgreSQL multi-worker, SQLite single-instance worker; Redis later

**V1 answer:** No job queue infrastructure in V1; everything runs synchronously within the HTTP request

**Why:** Original consumers (email, webhooks, automation) already removed; remaining synchronous-only work does not need a durable queue yet

**Est. Claude Code hours saved vs. original:** 15-25h

### 52. Outbound email — `DEFER_AFTER_V1`

**Original (52 in PRODUCT_DECISIONS_COMPLETE.md):** Pluggable outbound email backend: SMTP, sendmail, provider APIs later

**V1 answer:** Confirmed moot; superseded by Decision 14 (in-app-only notifications). Password reset email addressed separately in Decision 53.

**Why:** No email delivery backend needed without email notifications; savings already counted in Decision 14

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 14)

### 53. Password security — `SIMPLIFY_FOR_V1`

**Original (53 in PRODUCT_DECISIONS_COMPLETE.md):** Argon2id, strength checks, rate limiting, temporary lockout, email password reset, session invalidation after password change

**V1 answer:** Keep Argon2id/strength checks/rate limiting/lockout/session invalidation; replace email-based reset with admin-performed reset (sets temporary password), consistent with Decisions 2 and 52

**Why:** Resolves a real conflict: original decision assumed self-service email reset, but Decisions 2/52 removed both public registration and email; security fundamentals kept intact

**Est. Claude Code hours saved vs. original:** 1-2h

### 54. Web sessions versus API authentication — `KEEP_FOR_V1`

**Original (54 in PRODUCT_DECISIONS_COMPLETE.md):** Server-side cookie sessions for web; PAT/service tokens for API

**V1 answer:** Confirmed: cookie sessions for web with full HttpOnly/Secure/SameSite/CSRF/active-session management; PAT only for API (service tokens dropped per Decision 39)

**Why:** Core security pattern kept intact; only service-token half removed, already counted in Decision 39

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 39)

### 55. OIDC provisioning — `DEFER_AFTER_V1`

**Original (55 in PRODUCT_DECISIONS_COMPLETE.md):** Configurable per OIDC provider: auto-create vs pre-existing, domain restriction, default groups, group mapping

**V1 answer:** Confirmed moot; superseded by Decision 1 (OIDC removed entirely)

**Why:** No OIDC providers exist to configure; savings already counted in Decision 1

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 1)

### 56. User identity fields — `KEEP_FOR_V1`

**Original (56 in PRODUCT_DECISIONS_COMPLETE.md):** Immutable UUID, unique email, optional unique handle; display name not unique; internal references use UUID

**V1 answer:** Confirmed unchanged

**Why:** Core identity data model, consistent with the planned username->email/UUID/handle migration in NEXT.md

**Est. Claude Code hours saved vs. original:** 0h

### 57. User departure/duplicates — `SIMPLIFY_FOR_V1`

**Original (57 in PRODUCT_DECISIONS_COMPLETE.md):** Deactivate, anonymize, and merge accounts; merge transfers ownership/references to surviving account

**V1 answer:** Deactivation only; no anonymization, no account merge

**Why:** Merge existed mainly to reconcile local+OIDC duplicate identities, but OIDC was removed in Decision 1, eliminating its main justification

**Est. Claude Code hours saved vs. original:** 10-15h

### 58. Project visibility — `SIMPLIFY_FOR_V1`

**Original (58 in PRODUCT_DECISIONS_COMPLETE.md):** No separate Private/Internal/Public flag; visibility only through permission schemes

**V1 answer:** All authenticated users can view all projects; project roles (Decision 3) control who can edit, not who can see

**Why:** Replaces permission-scheme-based visibility with the simplest possible model now that fixed roles replaced schemes

**Est. Claude Code hours saved vs. original:** 0-2h

### 59. Anonymous access — `KEEP_FOR_V1`

**Original (59 in PRODUCT_DECISIONS_COMPLETE.md):** Optional read-only anonymous access, disabled by default: browse project/issues, explicitly public attachments

**V1 answer:** Keep optional read-only anonymous access as originally decided, despite it being somewhat orthogonal to the admin-created-accounts model from Decision 2

**Why:** User chose to preserve this despite the internal-tool framing; requires an anonymous Principal type in the authorization chain

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 60. Issue cloning — `SIMPLIFY_FOR_V1`

**Original (60 in PRODUCT_DECISIONS_COMPLETE.md):** Jira-like selectable cloning: attachments, links, sub-tasks, sprint, Epic, assignee, versions, custom fields; compatible target project; creates clones link

**V1 answer:** Simple cloning: copy summary/description/type/priority/labels/component into a new issue in the same project, create clones/is cloned by link; no selection dialog, no copying attachments/sub-tasks/links

**Why:** Sprint/versions/custom fields options already gone; simple field-copy clone is cheaper than a full selection dialog

**Est. Claude Code hours saved vs. original:** 6-10h

### 61. Issue templates — `DEFER_AFTER_V1`

**Original (61 in PRODUCT_DECISIONS_COMPLETE.md):** Global and project issue templates; prefill fields; optionally create sub-tasks, links, checklists, watchers

**V1 answer:** No templates in V1; users fill each issue manually (or use cloning from Decision 60)

**Why:** Separate template CRUD subsystem plus transactional structure generation is lower priority than core tracker for a small team

**Est. Claude Code hours saved vs. original:** 15-25h

### 62. Checklists — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (62 in PRODUCT_DECISIONS_COMPLETE.md):** Markdown checklist syntax only; no separate entities/assignees/due dates/history

**V1 answer:** Confirmed unchanged

**Why:** Covered entirely by the Markdown editor from Decision 16; already minimal

**Est. Claude Code hours saved vs. original:** 0h

### 63. Project ownership of an issue — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (63 in PRODUCT_DECISIONS_COMPLETE.md):** Exactly one project owns an issue; boards/filters may span projects, issues never do

**V1 answer:** Confirmed unchanged

**Why:** Core data invariant, already implemented in prototype

**Est. Claude Code hours saved vs. original:** 0h

### 64. Sub-task model — `KEEP_FOR_V1`

**Original (64 in PRODUCT_DECISIONS_COMPLETE.md):** Sub-task must have Story/Task/Bug parent, cannot exist independently, cannot have children, stays in parents project

**V1 answer:** Confirmed unchanged

**Why:** Already minimal hierarchy model consistent with fixed issue types

**Est. Claude Code hours saved vs. original:** 0h

### 65. Epic membership — `KEEP_FOR_V1`

**Original (65 in PRODUCT_DECISIONS_COMPLETE.md):** Optional, at most one Epic per Story/Task/Bug; Sub-tasks inherit via parent

**V1 answer:** Confirmed unchanged

**Why:** Already minimal: single optional FK field

**Est. Claude Code hours saved vs. original:** 0h

### 66. Backlog issues without Epic — `KEEP_FOR_V1`

**Original (66 in PRODUCT_DECISIONS_COMPLETE.md):** Dedicated No Epic group; unassigned issues visible and movable in/out of Epic

**V1 answer:** Keep as originally decided

**Why:** Simple UI grouping, no new database structure needed

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 67. Sub-task sprint assignment — `DEFER_AFTER_V1`

**Original (67 in PRODUCT_DECISIONS_COMPLETE.md):** Sub-task inherits parent sprint; cannot be scheduled separately

**V1 answer:** Confirmed moot; superseded by Decision 7 (Kanban-only)

**Why:** No sprints exist to inherit from; savings already counted in Decision 7

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 7)

### 68. Completing parent with unfinished sub-tasks — `SIMPLIFY_FOR_V1`

**Original (68 in PRODUCT_DECISIONS_COMPLETE.md):** Controlled by workflow validator; some transitions require sub-task completion, others allow completion/cancellation regardless

**V1 answer:** Fixed hardcoded rule: parent cannot transition to Done while any sub-task is unfinished; no configurable validator

**Why:** Resolves conflict with Decision 4 (no configurable validators); fixed safe default chosen over configurability

**Est. Claude Code hours saved vs. original:** 2-4h

### 69. Reopening parent and sub-tasks — `SIMPLIFY_FOR_V1`

**Original (69 in PRODUCT_DECISIONS_COMPLETE.md):** Controlled by workflow post-function; may leave, reopen all, or reopen selected-status sub-tasks

**V1 answer:** Fixed rule: reopening parent leaves sub-tasks unchanged; no configurable post-function

**Why:** Resolves conflict with Decision 4; simplest fixed default, user can manually reopen sub-tasks if needed

**Est. Claude Code hours saved vs. original:** 2-4h

### 70. Resolution on transition/reopen — `SIMPLIFY_FOR_V1`

**Original (70 in PRODUCT_DECISIONS_COMPLETE.md):** Controlled by workflow post-function: clear/preserve/set resolution on transition; reopen clears by default

**V1 answer:** Fixed rule: reopening always clears resolution; no configurable post-function

**Why:** Resolves conflict with Decision 4; consistent with manual resolution field from Decision 28

**Est. Claude Code hours saved vs. original:** 1-2h

### 71. Status categories — `SIMPLIFY_FOR_V1`

**Original (71 in PRODUCT_DECISIONS_COMPLETE.md):** Exactly To Do/In Progress/Done categories; admins create statuses but not categories

**V1 answer:** Categories exist only implicitly in the fixed workflow from Decision 4; no admin status editing

**Why:** Superseded by fixed single workflow; savings already counted in Decision 4

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 4)

### 72. Retiring a used status — `DEFER_AFTER_V1`

**Original (72 in PRODUCT_DECISIONS_COMPLETE.md):** Migration wizard: choose replacement status, review impact, confirm, audit trail

**V1 answer:** Confirmed moot; statuses are hardcoded and cannot be retired

**Why:** No status editing exists under the fixed workflow from Decision 4; savings already counted there

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 4)

### 73. Workflow editing/publishing — `DEFER_AFTER_V1`

**Original (73 in PRODUCT_DECISIONS_COMPLETE.md):** Editable draft, one published version, version history, publish-time status migration

**V1 answer:** Confirmed moot; workflow is hardcoded, no editor/draft/publish

**Why:** Superseded by fixed workflow from Decision 4; savings already counted there

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 4)

### 74. Transition conditions — `DEFER_AFTER_V1`

**Original (74 in PRODUCT_DECISIONS_COMPLETE.md):** Safe configurable conditions: roles/groups/users/assignee/reporter/lead/field values/prior status, AND/OR

**V1 answer:** Confirmed moot; anyone with project role access can perform any fixed transition

**Why:** Superseded by fixed workflow from Decision 4; savings already counted there

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 4)

### 75. Transition validators — `DEFER_AFTER_V1`

**Original (75 in PRODUCT_DECISIONS_COMPLETE.md):** Safe configurable validators: required fields, sub-task completion, assignee/Fix Version, numeric/date rules, permissions, counts, links, worklogs, related-issue status

**V1 answer:** Confirmed moot except the fixed sub-task-completion rule already set in Decision 68; no configurable validators otherwise

**Why:** Superseded by fixed workflow from Decision 4; sub-task case already handled as a hardcoded rule in Decision 68

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decisions 4 and 68)

### 76. Transition post-functions — `DEFER_AFTER_V1`

**Original (76 in PRODUCT_DECISIONS_COMPLETE.md):** Ordered configurable post-function chain plus mandatory internal history step

**V1 answer:** No configurable post-function chain; only the mandatory issue_history write remains on every transition

**Why:** Superseded by fixed workflow from Decision 4; resolution handling already fixed in Decisions 28/70

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decisions 4, 28, 70)

### 77. Transition forms — `SIMPLIFY_FOR_V1`

**Original (77 in PRODUCT_DECISIONS_COMPLETE.md):** Admin-chosen per-transition field list, order, required flags, defaults, comment field, help text

**V1 answer:** Fixed hardcoded transition forms (e.g. Done requires resolution field); no admin configuration

**Why:** Superseded by fixed workflow from Decision 4; savings already counted there

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 4)

### 78. SLA — `REMOVE_COMPLETELY`

**Original (78 in PRODUCT_DECISIONS_COMPLETE.md):** Not implemented; use due dates/estimates/worklogs/agile reports instead

**V1 answer:** Confirmed unchanged

**Why:** Was already never planned for implementation

**Est. Claude Code hours saved vs. original:** 0h

### 79. Issue voting — `KEEP_FOR_V1`

**Original (79 in PRODUCT_DECISIONS_COMPLETE.md):** One vote per authenticated user; visible count and voters; no automatic priority change

**V1 answer:** Keep voting as originally decided

**Why:** Cheap many-to-many table, similar cost to watchers; user chose to keep despite lower value for internal tracker use case

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 80. Mentions — `KEEP_FOR_V1`

**Original (80 in PRODUCT_DECISIONS_COMPLETE.md):** User mentions in all Markdown-capable fields notify mentioned user; group/role mentions deferred

**V1 answer:** Keep mentions with @handle autocomplete and in-app notification (via Decision 14 fixed notifications)

**Why:** Consistent with the full Markdown editor kept in Decision 16 and the handle field already planned in Decision 56

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 81. Edited comment history — `SIMPLIFY_FOR_V1`

**Original (81 in PRODUCT_DECISIONS_COMPLETE.md):** Keep all immutable comment edit versions with author/timestamp; authorized users can view previous versions; no short edit window

**V1 answer:** Just an edited (timestamp) flag; no storage of previous comment text versions

**Why:** Full version history requires a separate comment-versions table and viewer UI; edited flag alone is much cheaper

**Est. Claude Code hours saved vs. original:** 6-10h

### 82. Deleted comments — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (82 in PRODUCT_DECISIONS_COMPLETE.md):** Tombstone for users; original content retained in admin audit

**V1 answer:** Keep tombstone; content stays in DB (hidden from normal users) and is viewable by admins directly, no separate admin UI required beyond existing soft-delete columns

**Why:** Existing deleted_at/deleted_by_user_id columns already cover this; no new subsystem needed

**Est. Claude Code hours saved vs. original:** 0h

### 83. Comment permissions — `SIMPLIFY_FOR_V1`

**Original (83 in PRODUCT_DECISIONS_COMPLETE.md):** Separate permissions: edit own, edit all, delete own, delete all

**V1 answer:** Simplified: author can edit/delete own comment; project/global admin can edit/delete any comment; no separate 4-permission matrix

**Why:** 4-permission matrix was designed for configurable permission schemes replaced by fixed roles in Decision 3

**Est. Claude Code hours saved vs. original:** 2-4h

### 84. Emoji reactions — `KEEP_FOR_V1`

**Original (84 in PRODUCT_DECISIONS_COMPLETE.md):** Fixed reaction set on comments only; each user each reaction once per comment

**V1 answer:** Keep emoji reactions as originally decided

**Why:** Cheap many-to-many table similar to watchers/voting; user chose to keep despite being a cosmetic feature

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 85. Comment threading — `KEEP_FOR_V1`

**Original (85 in PRODUCT_DECISIONS_COMPLETE.md):** No threads; single chronological discussion with mentions, citations, links to specific comments

**V1 answer:** Confirmed unchanged

**Why:** Already minimal, no further reduction possible

**Est. Claude Code hours saved vs. original:** 0h

### 86. Personal notification preferences — `DEFER_AFTER_V1`

**Original (86 in PRODUCT_DECISIONS_COMPLETE.md):** Per-event/channel controls plus digests; security/critical notices cannot be disabled

**V1 answer:** Confirmed moot; fixed notification set for everyone per Decision 14, no per-user preferences

**Why:** Superseded by fixed in-app notifications from Decision 14; savings already counted there

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 14)

### 87. Project archival — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (87 in PRODUCT_DECISIONS_COMPLETE.md):** Read-only archive: leaves active lists, viewable, cannot be modified, restorable

**V1 answer:** Confirmed unchanged; use existing archived/archived_at columns plus write-time read-only enforcement

**Why:** archived/archived_at columns already exist in prototype; only read-only write enforcement remains to add

**Est. Claude Code hours saved vs. original:** 0-2h

### 88. Permanent project deletion — `SIMPLIFY_FOR_V1`

**Original (88 in PRODUCT_DECISIONS_COMPLETE.md):** Require complete verified export before entering recycle bin, then admin restore or permanent delete

**V1 answer:** Drop the mandatory-export precondition; project goes straight to recycle bin, admin may manually CSV-export first if desired but it is not enforced

**Why:** Mandatory export enforcement adds complexity not justified after Decision 48 reduced export to simple read-only CSV

**Est. Claude Code hours saved vs. original:** 3-6h

### 89. Project recycle-bin retention — `SIMPLIFY_FOR_V1`

**Original (89 in PRODUCT_DECISIONS_COMPLETE.md):** Configurable retention, default 90 days; 30/90/365 or never

**V1 answer:** Fixed 90-day retention, no admin configuration; expiry checked on-demand when viewing recycle bin, no background job

**Why:** Configurable retention needs admin settings plus a background sweep job, but job infrastructure was removed in Decision 51

**Est. Claude Code hours saved vs. original:** 5-10h

### 90. Project key while project is in bin — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (90 in PRODUCT_DECISIONS_COMPLETE.md):** Key stays reserved while project in recycle bin; not reusable until permanent deletion

**V1 answer:** Confirmed unchanged

**Why:** Core invariant, consistent with existing project_key_aliases table

**Est. Claude Code hours saved vs. original:** 0h

### 91. Changing active project key — `KEEP_FOR_V1`

**Original (91 in PRODUCT_DECISIONS_COMPLETE.md):** Allowed with permanent old issue-key aliases; numeric suffixes remain

**V1 answer:** Confirmed unchanged

**Why:** Small feature built on existing alias infrastructure from Decisions 38/90

**Est. Claude Code hours saved vs. original:** 0-2h

### 92. Issue number reuse — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (92 in PRODUCT_DECISIONS_COMPLETE.md):** Never reuse, even after permanent deletion

**V1 answer:** Confirmed unchanged

**Why:** Core invariant already implemented via transactional number allocation

**Est. Claude Code hours saved vs. original:** 0h

### 93. Project key syntax — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (93 in PRODUCT_DECISIONS_COMPLETE.md):** Uppercase letters/digits, starts with letter, length 2-12, regex ^[A-Z][A-Z0-9]{1,11}$

**V1 answer:** Confirmed unchanged

**Why:** Already implemented in prototype validation

**Est. Claude Code hours saved vs. original:** 0h

### 94. Case handling for keys — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (94 in PRODUCT_DECISIONS_COMPLETE.md):** Case-insensitive input, uppercase canonical form across URLs/API/search/imports/Git

**V1 answer:** Confirmed; scope now just URL/API/UI input since imports and Git recognition were removed

**Why:** Already partly implemented; import/Git surfaces moot after Decisions 42/48

**Est. Claude Code hours saved vs. original:** 0h

### 95. Initial issue number — `SIMPLIFY_FOR_V1`

**Original (95 in PRODUCT_DECISIONS_COMPLETE.md):** Admin-selectable at project creation, default 1; never lowered/reused once creation begins

**V1 answer:** Always starts at 1, no admin choice at project creation

**Why:** Value existed mainly to support migration continuity, but Jira migration was removed in Decision 48

**Est. Claude Code hours saved vs. original:** 1-2h

### 96. Gaps in issue numbering — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (96 in PRODUCT_DECISIONS_COMPLETE.md):** Gaps allowed; failed/rolled-back creation may consume a number

**V1 answer:** Confirmed unchanged

**Why:** Natural consequence of already-implemented transactional number allocation from Decision 92

**Est. Claude Code hours saved vs. original:** 0h

### 97. Labels — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (97 in PRODUCT_DECISIONS_COMPLETE.md):** Free-form labels, autocomplete, case-insensitive normalization

**V1 answer:** Confirmed unchanged

**Why:** Trim/lowercase/dedup already implemented in prototype

**Est. Claude Code hours saved vs. original:** 0h

### 98. Attachment limits/security — `SIMPLIFY_FOR_V1`

**Original (98 in PRODUCT_DECISIONS_COMPLETE.md):** Configurable size/count/MIME allow-deny/extension/quota limits; no antivirus/DLP

**V1 answer:** Fixed reasonable limits (e.g. 25MB/file, 20 attachments/issue, blocked dangerous extensions), no admin configuration, no quotas

**Why:** Full admin configuration subsystem for limits/quotas not justified vs fixed sane defaults

**Est. Claude Code hours saved vs. original:** 8-12h

### 99. Attachment previews — `KEEP_FOR_V1`

**Original (99 in PRODUCT_DECISIONS_COMPLETE.md):** Safe previews: images, PDF, text/source, browser-supported audio/video; others download-only

**V1 answer:** Keep all 4 preview types using native browser elements (img, embed/iframe for PDF, audio/video tags); no custom PDF.js or heavy libraries

**Why:** Most preview types map to native HTML elements rather than custom libraries, keeping cost reasonable

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 100. Inline images in Markdown — `KEEP_FOR_V1`

**Original (100 in PRODUCT_DECISIONS_COMPLETE.md):** Ordinary attachments referenced via attachment://UUID; upload, drag/drop, clipboard paste; no unrestricted external embedding

**V1 answer:** Keep full upload + drag/drop + paste support in the Markdown editor

**Why:** Natural extension of the full Markdown editor (Decision 16) and local attachment storage (Decision 15)

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 101. Attachment listing/deletion — `KEEP_FOR_V1`

**Original (101 in PRODUCT_DECISIONS_COMPLETE.md):** Sortable list (name/size/date/author/type) plus recycle bin; deletion leaves removed marker in Markdown; admin restore/purge

**V1 answer:** Keep sortable list and recycle bin for attachments

**Why:** Consistent with the same soft-delete pattern already used for issues/comments/projects

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 102. Attachment recycle-bin retention — `SIMPLIFY_FOR_V1`

**Original (102 in PRODUCT_DECISIONS_COMPLETE.md):** Configurable retention, default 90 days; verify no valid reference before purge

**V1 answer:** Fixed 90-day retention, on-demand check, no admin config, no background job (consistent with Decision 89)

**Why:** Same pattern as project recycle-bin retention in Decision 89, consistent with job infrastructure removal in Decision 51

**Est. Claude Code hours saved vs. original:** 3-5h

### 103. Duplicate attachment filename — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (103 in PRODUCT_DECISIONS_COMPLETE.md):** Create independent immutable attachment; same filename allowed multiple times

**V1 answer:** Confirmed unchanged

**Why:** Already implemented via attachments table design

**Est. Claude Code hours saved vs. original:** 0h

### 104. Physical attachment deduplication — `KEEP_FOR_V1`

**Original (104 in PRODUCT_DECISIONS_COMPLETE.md):** None initially; every upload stores independent physical object even if identical

**V1 answer:** Confirmed unchanged

**Why:** Already the cheapest option; deduplication would add reference-counting complexity

**Est. Claude Code hours saved vs. original:** 0h

### 105. Attachment integrity — `SIMPLIFY_FOR_V1`

**Original (105 in PRODUCT_DECISIONS_COMPLETE.md):** Verify at upload and via periodic background audits; report missing/altered/corrupt objects

**V1 answer:** Upload-time SHA-256/size verification only; no periodic audit mechanism at all

**Why:** Periodic background audits require job infrastructure removed in Decision 51; user chose to drop this entirely rather than add a CLI-triggered variant

**Est. Claude Code hours saved vs. original:** 5-8h

### 106. Backup layout for attachments — `SIMPLIFY_FOR_V1`

**Original (106 in PRODUCT_DECISIONS_COMPLETE.md):** Database/config and attachment objects separate, joined by checksummed manifest for scalable consistent restore

**V1 answer:** Simple approach: copy attachment directory plus a DB dump; no separate checksum manifest file

**Why:** Attachments are local-filesystem-only now (Decision 15), making manifest-based reconciliation unnecessary overhead

**Est. Claude Code hours saved vs. original:** 5-8h

### 107. Backup consistency mode — `SIMPLIFY_FOR_V1`

**Original (107 in PRODUCT_DECISIONS_COMPLETE.md):** Both offline/read-only and online snapshot; PostgreSQL defaults to online, SQLite short read-only window

**V1 answer:** Offline/maintenance-window backup only for both databases; no online consistent snapshot logic

**Why:** Online consistent snapshot requires nontrivial transaction/replication-slot handling; simpler maintenance-window approach sufficient for small deployments

**Est. Claude Code hours saved vs. original:** 10-15h

### 108. Restore safety — `SIMPLIFY_FOR_V1`

**Original (108 in PRODUCT_DECISIONS_COMPLETE.md):** Restore into isolated temporary environment, verify manifest/checksums/schema/integrity, then atomic switch, keep old data for rollback

**V1 answer:** Direct restore into target database via ticket-hub restore command with a confirmation warning; admin responsible for their own pre-restore backup; no isolated staging environment

**Why:** Isolated-environment restore orchestration is nontrivial; direct restore with a clear warning is acceptable for small self-hosted deployments

**Est. Claude Code hours saved vs. original:** 15-25h

### 109. Backup version direction — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (109 in PRODUCT_DECISIONS_COMPLETE.md):** Forward migrate older supported backups; reject newer backups in older app; no downgrade

**V1 answer:** Confirmed unchanged

**Why:** Natural consequence of the existing ordered checksummed migration system

**Est. Claude Code hours saved vs. original:** 0-2h

### 110. Very old backup compatibility — `DEFER_AFTER_V1`

**Original (110 in PRODUCT_DECISIONS_COMPLETE.md):** Documented LTS migration checkpoints for very old backups (bridge releases)

**V1 answer:** Defer entirely; no LTS bridge planning needed for a first V1 release with no legacy backups to migrate

**Why:** Premature multi-year version planning for a product that has no prior versions yet

**Est. Claude Code hours saved vs. original:** 5-10h

### 111. Application upgrades — `SIMPLIFY_FOR_V1`

**Original (111 in PRODUCT_DECISIONS_COMPLETE.md):** Staged upgrade check/apply plus rolling upgrades where possible; preflight checks DB/schema/space/storage/jobs/backup/migration path

**V1 answer:** Use existing ticket-hub migrate command only; no separate upgrade check/apply wizard, no rolling upgrades

**Why:** Rolling upgrades moot after Kubernetes/multi-instance removal (Decision 50) and job infra removal (Decision 51); existing migrate command suffices

**Est. Claude Code hours saved vs. original:** 10-15h

### 112. Automatic application updates — `SIMPLIFY_FOR_V1`

**Original (112 in PRODUCT_DECISIONS_COMPLETE.md):** Notify only; checks and reports available versions/release notes/security alerts, never auto-installs

**V1 answer:** Simple in-app admin banner when a newer version is available; no email delivery (email removed in Decision 14/52)

**Why:** Already the cheapest concept; only the delivery channel changed from email to in-app since email was removed

**Est. Claude Code hours saved vs. original:** 0-2h

### 113. Release channels — `DEFER_AFTER_V1`

**Original (113 in PRODUCT_DECISIONS_COMPLETE.md):** Stable, LTS, Preview release channels

**V1 answer:** No release channels in V1; single version line with semver tags

**Why:** Multiple support channels only matter once several versions with different lifecycles exist; premature for a first release

**Est. Claude Code hours saved vs. original:** 3-5h

### 114. Changing database backend — `DEFER_AFTER_V1`

**Original (114 in PRODUCT_DECISIONS_COMPLETE.md):** Database-neutral application export/import for SQLite<->PostgreSQL migration, rebuilding FTS indexes

**V1 answer:** No migration tool in V1; admin picks a database at install time and stays on it

**Why:** Standalone cross-engine data migration tool remains expensive even after FTS simplification in Decision 43

**Est. Claude Code hours saved vs. original:** 15-25h

### 115. Inbound email features — `DEFER_AFTER_V1`

**Original (115 in PRODUCT_DECISIONS_COMPLETE.md):** Create issues and turn replies into comments; subject/body/attachment mapping, sender mapping, anti-spoofing/loop protection

**V1 answer:** No inbound email in V1; issues/comments created via UI only. Confirms entire block 116-123 as moot.

**Why:** Outbound email backend was removed in Decision 52; inbound email has no purpose without it, and user explicitly wanted no inbound email in V1

**Est. Claude Code hours saved vs. original:** 40-60h (covers block 115-123)

### 116. Unknown inbound email sender — `DEFER_AFTER_V1`

**Original (116 in PRODUCT_DECISIONS_COMPLETE.md):** Always reject unknown sender; only existing active user may create/comment by email

**V1 answer:** Confirmed moot; no inbound email in V1

**Why:** Depends on inbound email removed in Decision 115

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 115)

### 117. Reply trimming — `DEFER_AFTER_V1`

**Original (117 in PRODUCT_DECISIONS_COMPLETE.md):** Reply-above-marker with conservative fallback stripping

**V1 answer:** Confirmed moot; no inbound email in V1

**Why:** Depends on inbound email removed in Decision 115

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 115)

### 118. Inbound email body format — `DEFER_AFTER_V1`

**Original (118 in PRODUCT_DECISIONS_COMPLETE.md):** Prefer text/plain; sanitized HTML-to-Markdown fallback

**V1 answer:** Confirmed moot; no inbound email in V1

**Why:** Depends on inbound email removed in Decision 115

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 115)

### 119. Duplicate filenames in inbound email — `DEFER_AFTER_V1`

**Original (119 in PRODUCT_DECISIONS_COMPLETE.md):** Store all as separate immutable attachments; filenames not unique

**V1 answer:** Confirmed moot; no inbound email in V1

**Why:** Depends on inbound email removed in Decision 115

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 115)

### 120. Inbound mail transport — `DEFER_AFTER_V1`

**Original (120 in PRODUCT_DECISIONS_COMPLETE.md):** Pluggable IMAP, mail pipe, HTTP webhook adapters

**V1 answer:** Confirmed moot; no inbound email in V1

**Why:** Depends on inbound email removed in Decision 115

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 115)

### 121. IMAP delivery mode — `DEFER_AFTER_V1`

**Original (121 in PRODUCT_DECISIONS_COMPLETE.md):** IMAP IDLE with configurable polling fallback

**V1 answer:** Confirmed moot; no inbound email in V1

**Why:** Depends on inbound email removed in Decision 115

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 115)

### 122. Inbound email idempotency — `DEFER_AFTER_V1`

**Original (122 in PRODUCT_DECISIONS_COMPLETE.md):** Message-ID/UID/checksum-based idempotency plus quarantine

**V1 answer:** Confirmed moot; no inbound email in V1

**Why:** Depends on inbound email removed in Decision 115

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 115)

### 123. Inbound email processing failure — `DEFER_AFTER_V1`

**Original (123 in PRODUCT_DECISIONS_COMPLETE.md):** Retry with backoff then dead-letter queue; atomic processing

**V1 answer:** Confirmed moot; no inbound email in V1

**Why:** Depends on inbound email removed in Decision 115; concludes the email block

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 115)

### 124. REST rate limiting — `SIMPLIFY_FOR_V1`

**Original (124 in PRODUCT_DECISIONS_COMPLETE.md):** Configurable multi-level rate limits by IP/user/token/service account/endpoint/installation; 429 + Retry-After

**V1 answer:** Simple fixed rate limit per IP/user (e.g. login and write endpoints); no admin config, no per-endpoint/service-account exceptions

**Why:** Multi-level limits were designed for public API with service accounts, simplified after Decision 39; basic brute-force protection still kept

**Est. Claude Code hours saved vs. original:** 10-15h

### 125. REST request and batch limits — `SIMPLIFY_FOR_V1`

**Original (125 in PRODUCT_DECISIONS_COMPLETE.md):** Endpoint-specific body/item limits with audited service-account exceptions; covers imports/attachments/bulk/filters/webhooks/page size

**V1 answer:** Fixed constants only (max body size, max bulk items, max page size); no admin exceptions

**Why:** Service accounts, imports, and webhooks already removed, shrinking scope to a few simple hardcoded limits

**Est. Claude Code hours saved vs. original:** 3-5h

### 126. REST pagination — `SIMPLIFY_FOR_V1`

**Original (126 in PRODUCT_DECISIONS_COMPLETE.md):** Cursor pagination preferred for public API; numbered/offset available where suitable

**V1 answer:** Numbered/offset pagination everywhere; no cursor mechanism

**Why:** Cursor pagination targeted high-volume public API use, unnecessary for a small internal tracker with a simplified PAT-only API

**Est. Claude Code hours saved vs. original:** 5-10h

### 127. REST API versioning — `KEEP_FOR_V1`

**Original (127 in PRODUCT_DECISIONS_COMPLETE.md):** Major version in URL (/api/v1); backward-compatible evolution; /api/v2 with deprecation period for breaking changes

**V1 answer:** Use /api/v1 prefix now; formal deprecation policy deferred until a real /api/v2 is needed

**Why:** URL prefix is trivial to add; formal versioning process only matters once a v2 actually exists

**Est. Claude Code hours saved vs. original:** 0-2h

### 128. REST write idempotency keys — `DEFER_AFTER_V1`

**Original (128 in PRODUCT_DECISIONS_COMPLETE.md):** Required for high-risk operations (issue/comment/worklog creation, bulk changes, imports); optional for simpler updates

**V1 answer:** No idempotency-key mechanism in V1; accept small risk of duplicate records on client retries

**Why:** Import use case already removed; lower priority for a small internal tracker with direct UI/PAT access

**Est. Claude Code hours saved vs. original:** 8-12h

### 129. Concurrent issue editing — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (129 in PRODUCT_DECISIONS_COMPLETE.md):** Optimistic locking with version; stale writes fail (409); UI shows conflicting changes and allows reload/reapply

**V1 answer:** Keep full behavior: server-side version conflict rejection (already partly done) plus UI dialog for reload/reapply

**Why:** version column and stale-write detection already exist in prototype for status changes

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 130. Realtime browser updates — `DEFER_AFTER_V1`

**Original (130 in PRODUCT_DECISIONS_COMPLETE.md):** SSE for issue/comment/board/sprint/notification updates with polling fallback; multi-node event distribution

**V1 answer:** No realtime mechanism in V1 at all; users refresh the page manually to see changes

**Why:** Multi-node distribution already moot after Decisions 50/51; base SSE still nontrivial and lower priority than core tracker features

**Est. Claude Code hours saved vs. original:** 15-25h

### 131. Internal event distribution — `DEFER_AFTER_V1`

**Original (131 in PRODUCT_DECISIONS_COMPLETE.md):** Pluggable durable DB event log plus PostgreSQL LISTEN/NOTIFY; SQLite local event loop; Redis Streams later

**V1 answer:** Confirmed moot; no realtime mechanism (Decision 130), no job infra (Decision 51)

**Why:** Nothing left to distribute after Decisions 130/51

**Est. Claude Code hours saved vs. original:** 0h (already counted in Decision 130)

### 132. Caching — `DEFER_AFTER_V1`

**Original (132 in PRODUCT_DECISIONS_COMPLETE.md):** Pluggable caching; default per-instance in-memory with event-bus invalidation; DB remains source of truth; Redis optional later

**V1 answer:** No caching layer in V1; all queries hit the database directly

**Why:** Event-bus invalidation already moot after Decision 131; low value for small self-hosted deployment where DB is fast enough

**Est. Claude Code hours saved vs. original:** 10-15h

### 133. Observability — `SIMPLIFY_FOR_V1`

**Original (133 in PRODUCT_DECISIONS_COMPLETE.md):** Pluggable structured JSON logs, Prometheus metrics, OpenTelemetry traces, correlation IDs across HTTP/DB/jobs/email/webhooks

**V1 answer:** Structured JSON logs to stdout only; no Prometheus, no OpenTelemetry

**Why:** Cross-system correlation moot after jobs/email/webhooks removal; stdout logs sufficient for small self-hosted deployment (captured by Docker/systemd)

**Est. Claude Code hours saved vs. original:** 15-25h

### 134. Secrets management — `SIMPLIFY_FOR_V1`

**Original (134 in PRODUCT_DECISIONS_COMPLETE.md):** Pluggable secrets backend: env vars, Docker/Kubernetes secrets, restricted files, encrypted DB values, future Vault

**V1 answer:** Environment variables only (.env file / Docker secrets); no pluggable backend, no encrypted DB values, no Vault

**Why:** Kubernetes secrets moot after Decision 50; env vars are the standard sufficient approach for small Docker Compose deployments

**Est. Claude Code hours saved vs. original:** 8-12h

### 135. Encryption at rest — `SIMPLIFY_FOR_V1`

**Original (135 in PRODUCT_DECISIONS_COMPLETE.md):** Pluggable policy; infrastructure encryption default; optional application encryption for attachments and sensitive custom fields with external keys

**V1 answer:** Rely on infrastructure/disk-level encryption only; no application-level encryption in Ticket Hub code

**Why:** Custom-fields half already moot after Decision 9; application encryption for attachments not justified for V1

**Est. Claude Code hours saved vs. original:** 10-15h

### 136. Issue recycle-bin retention — `SIMPLIFY_FOR_V1`

**Original (136 in PRODUCT_DECISIONS_COMPLETE.md):** Configurable retention, default 90 days: 30/90/365 or never

**V1 answer:** Fixed 90-day retention, on-demand check, no admin config, no background job (consistent with Decisions 89 and 102)

**Why:** Same pattern as project and attachment recycle-bin retention

**Est. Claude Code hours saved vs. original:** 3-5h

### 137. SQLite product scope — `KEEP_FOR_V1`

**Original (137 in PRODUCT_DECISIONS_COMPLETE.md):** Same user-facing features with operational limits; one server process, limited worker concurrency, no horizontal scaling; no deliberately cut-down edition

**V1 answer:** Keep both: PostgreSQL primary, SQLite with full feature parity, as originally decided

**Why:** User chose to preserve the dual-database promise despite the recurring cost this creates across many other decisions (FTS, backup, etc.)

**Est. Claude Code hours saved vs. original:** 0h (kept as-is)

### 138. Server platforms — `KEEP_FOR_V1`

**Original (138 in PRODUCT_DECISIONS_COMPLETE.md):** Linux fully supported; Windows/macOS best-effort buildable/runnable

**V1 answer:** Confirmed unchanged

**Why:** Already minimal; no testing/support commitments beyond Linux, consistent with Docker-only distribution (Decision 50)

**Est. Claude Code hours saved vs. original:** 0h

### 139. Browser support — `KEEP_FOR_V1`

**Original (139 in PRODUCT_DECISIONS_COMPLETE.md):** Latest two major Chrome/Firefox/Edge/Safari; progressive degradation for older; no IE

**V1 answer:** Confirmed unchanged

**Why:** Natural consequence of modern vanilla JS without polyfills; no extra work required

**Est. Claude Code hours saved vs. original:** 0h

### 140. Frontend architecture — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (140 in PRODUCT_DECISIONS_COMPLETE.md):** Crow serves real HTML/URLs; vanilla JS progressive enhancement for nav/boards/dialogs/filters/forms/REST/SSE; no framework; no initial PWA

**V1 answer:** Confirmed; hybrid HTML + vanilla JS as core non-negotiable identity, SSE mention dropped as moot after Decision 130

**Why:** Already implemented in prototype (Crow + vanilla HTML/CSS/JS); core non-negotiable product identity from CLAUDE.md

**Est. Claude Code hours saved vs. original:** 0h

### 141. License — `ALREADY_IMPLEMENTED_AND_KEEP`

**Original (141 in PRODUCT_DECISIONS_COMPLETE.md):** MIT License

**V1 answer:** Confirmed unchanged

**Why:** Legal choice, no implementation cost, LICENSE file already exists in prototype

**Est. Claude Code hours saved vs. original:** 0h

## Closed scope

This questionnaire pass is complete: all 142 original decisions (0-141) received an explicit V1 classification during an interactive session with the product owner on 2026-07-31; none were silently carried forward. Re-opening a decision after this point requires a new explicit product conversation, not a unilateral implementation choice. See `REDUCED_SCOPE_SPECIFICATION.md` for the resulting V1 product baseline, `REMOVED_AND_DEFERRED_FEATURES.md` for what is intentionally absent, and `REDUCED_SCOPE_ROADMAP.md` for how V1 gets built.
