# Removed and deferred features (V1)

This is the authoritative list of everything that is **not** in Ticket Hub V1, split into two permanently different categories. Both lists are decision-numbered (`D#`) against `docs/REDUCED_SCOPE_DECISIONS.md` / `docs/PRODUCT_DECISIONS_COMPLETE.md`.

- **Permanently removed** — no longer part of the Ticket Hub plan at all, at any future point, unless a fresh product conversation explicitly reopens it.
- **Deferred after V1** — a real, wanted feature, intentionally cut from V1 for cost, and a legitimate candidate for a post-V1 milestone.

Totals: **7 permanently removed**, **38 deferred after V1** (of 142 decisions total; the remaining 97 were kept, simplified, or already implemented — see `docs/REDUCED_SCOPE_DECISIONS.md`).

## Permanently removed

| D# | Topic | What was removed |
|---|---|---|
| 1 | Authentication providers | Local accounts only; OIDC removed entirely from V1 (not even an architecture stub) |
| 5 | Issue hierarchy direction | Drop the future-extensibility-above-Epic note entirely; hierarchy is fixed at Epic -> Story/Task/Bug -> Sub-task only |
| 8 | Project management style | Drop the team-managed future note entirely; distinction is moot now that roles/workflow are fixed rather than schemed |
| 21 | Issue-level security | Confirmed unchanged |
| 26 | Recurring issues | Confirmed unchanged |
| 48 | Import/export | Simple CSV export of issues (read-only); no CSV import, no Jira migration tool; backup/restore handled separately in Decisions 106-110 |
| 78 | SLA | Confirmed unchanged |

## Deferred after V1

Grouped by theme for readability; original decision order preserved within each group.

### Custom fields and saved filters

- **D9 — Custom fields and screens:** No custom fields in V1 at all; only the fixed standard issue field set
- **D35 — Quick filters:** No quick filters in V1; board uses only ad-hoc UI filtering

### Scrum, sprints, and agile reports

- **D11 — Agile reports:** No agile reports in V1 at all, including CFD/control chart; burndown/sprint report/velocity already moot after Scrum removal (Decision 7)
- **D30 — Sprints:** Confirmed moot; superseded by Decision 7 (Kanban-only)
- **D34 — Swimlanes:** No swimlanes in V1; board is a flat per-column list
- **D67 — Sub-task sprint assignment:** Confirmed moot; superseded by Decision 7 (Kanban-only)

### Configurable workflow engine (consequence of Decision 4)

- **D72 — Retiring a used status:** Confirmed moot; statuses are hardcoded and cannot be retired
- **D73 — Workflow editing/publishing:** Confirmed moot; workflow is hardcoded, no editor/draft/publish
- **D74 — Transition conditions:** Confirmed moot; anyone with project role access can perform any fixed transition
- **D75 — Transition validators:** Confirmed moot except the fixed sub-task-completion rule already set in Decision 68; no configurable validators otherwise
- **D76 — Transition post-functions:** No configurable post-function chain; only the mandatory issue_history write remains on every transition

### Versions, releases, and templates

- **D18 — Versions and releases:** No versions/releases concept in V1 at all; labels can informally track release if needed
- **D61 — Issue templates:** No templates in V1; users fill each issue manually (or use cloning from Decision 60)

### Automation

- **D25 — Automation:** No automation in V1 at all; all actions performed manually

### Public API surface: webhooks, Git links, extensions, idempotency

- **D41 — Webhook filtering:** Confirmed moot; superseded by Decision 39 (no webhooks in V1)
- **D42 — Git integration:** No Git integration in V1; no mechanism for external tools to attach links without public API/webhooks
- **D49 — Extensions:** Confirmed moot; depends on public API/webhooks/service accounts removed in Decision 39
- **D128 — REST write idempotency keys:** No idempotency-key mechanism in V1; accept small risk of duplicate records on client retries

### Email — outbound backend and the entire inbound subsystem

- **D52 — Outbound email:** Confirmed moot; superseded by Decision 14 (in-app-only notifications). Password reset email addressed separately in Decision 53.
- **D55 — OIDC provisioning:** Confirmed moot; superseded by Decision 1 (OIDC removed entirely)
- **D86 — Personal notification preferences:** Confirmed moot; fixed notification set for everyone per Decision 14, no per-user preferences
- **D115 — Inbound email features:** No inbound email in V1; issues/comments created via UI only. Confirms entire block 116-123 as moot.
- **D116 — Unknown inbound email sender:** Confirmed moot; no inbound email in V1
- **D117 — Reply trimming:** Confirmed moot; no inbound email in V1
- **D118 — Inbound email body format:** Confirmed moot; no inbound email in V1
- **D119 — Duplicate filenames in inbound email:** Confirmed moot; no inbound email in V1
- **D120 — Inbound mail transport:** Confirmed moot; no inbound email in V1
- **D121 — IMAP delivery mode:** Confirmed moot; no inbound email in V1
- **D122 — Inbound email idempotency:** Confirmed moot; no inbound email in V1
- **D123 — Inbound email processing failure:** Confirmed moot; no inbound email in V1

### Background jobs, realtime updates, and caching

- **D51 — Background jobs:** No job queue infrastructure in V1; everything runs synchronously within the HTTP request
- **D130 — Realtime browser updates:** No realtime mechanism in V1 at all; users refresh the page manually to see changes
- **D131 — Internal event distribution:** Confirmed moot; no realtime mechanism (Decision 130), no job infra (Decision 51)
- **D132 — Caching:** No caching layer in V1; all queries hit the database directly

### Backup era-compatibility, release channels, database-backend migration

- **D110 — Very old backup compatibility:** Defer entirely; no LTS bridge planning needed for a first V1 release with no legacy backups to migrate
- **D113 — Release channels:** No release channels in V1; single version line with semver tags
- **D114 — Changing database backend:** No migration tool in V1; admin picks a database at install time and stays on it

### Internationalization framework

- **D44 — Internationalization:** English only, no i18n framework in V1; text hardcoded in UI/templates

## Revisiting a deferred feature

Do not silently re-add anything from the deferred list mid-implementation. If a milestone genuinely needs one of these, treat it the same as any other product-scope change: raise it explicitly with the product owner, record a new decision (with a decision number continuing from 141), and update `docs/REDUCED_SCOPE_DECISIONS.md` and this file together.
