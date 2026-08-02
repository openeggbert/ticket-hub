# Ticket Hub plan

The authoritative, continuously updated plan is [NEXT.md](NEXT.md); the phased roadmap it tracks against
is [docs/REDUCED_SCOPE_ROADMAP.md](docs/REDUCED_SCOPE_ROADMAP.md) (the original
[docs/ROADMAP.md](docs/ROADMAP.md) is long-term reference only). This file is a short pointer plus a
snapshot status line, kept in sync at each milestone boundary -- see `NEXT.md` for full batch-by-batch
detail and `docs/VERIFICATION.md` for exactly what was tested and how.

## Current status (2026-08-02)

- **Milestone 1** (Phases 1-3: identity/sessions, authorization/projects, issue core/fixed workflow) --
  **complete** at the core/CLI/test/server/UI layer, with one deliberate exception: re-typing
  (`issueTypeKey`) or re-parenting (`parentIssueKey`) an issue after creation is not implemented.
- **Milestone 2** (Phase 4: Collaboration; Phase 5: Attachments and Kanban board) -- **complete**. Comment
  editing/tombstone delete, fixed emoji reactions, @mention handles and in-app notifications, the
  Markdown editor (toolbar/live preview/full attachment upload+drag-drop+paste), simplified worklogs, the
  admin/security audit log, ad-hoc issue filter/search widening, the personal dashboard, Kanban board WIP
  limits, and the full attachments vertical (local filesystem storage, four native-element previews,
  sortable list, 90-day recycle bin) are all implemented, tested on both PostgreSQL and SQLite, and
  browser-verified end-to-end with Playwright/Chromium.
- **Milestone 3** (Phase 6: API/security hardening/export; Phase 7: backup/restore/upgrade) --
  **Phase 6 complete**: personal access tokens (D39/D40), the active-session list/"sign out everywhere"
  endpoint (D54), fixed rate limits (D124/D125), the versioned `/api/v1` prefix (D127), read-only CSV
  export (D48), fixed request-body/bulk-item constants (D125), the security hardening pass (found and
  fixed a real stored-XSS vulnerability in attachment preview/download), and numbered/offset pagination
  (D126) for `GET /api/v1/issues` (a deliberate partial rollout, every other list endpoint documented as
  still open). **Phase 7 started**: backup and restore (D106-D108) are done --
  `ticket-hub-cli backup <dir>` / `restore <dir> --yes`, live-verified end-to-end on both SQLite and
  PostgreSQL matching the exit gate exactly (seed → backup → destroy → restore, data and an attachment
  file round-tripping correctly). D111 (upgrades) needed no new work -- `ticket-hub-cli migrate` already
  satisfies it. Still open: the in-app admin version banner (D112) and structured JSON logs to stdout
  (D133). No web UI yet for managing tokens or sessions.
- **Milestone 4** (Phase 8: packaging and hardening) -- not started.

## Implementation rules

- Preserve buildability and tests after every change; run `ctest --output-on-failure` before finishing a
  batch, on both database backends and in every supported build configuration.
- Keep PostgreSQL and SQLite behavior aligned at the application-contract level.
- Never introduce generic SQL into application/domain modules.
- Schema migration files are immutable once applied; add a new ordered migration instead of editing one.
- Side effects must eventually use durable jobs/outbox events, not detached in-memory work -- and V1 has
  no job infrastructure at all (`docs/REMOVED_AND_DEFERRED_FEATURES.md`), so anything that would need one
  is either simplified to an on-demand check or dropped.
- Do not implement anything from `docs/REMOVED_AND_DEFERRED_FEATURES.md` without an explicit new product
  conversation, and do not jump ahead to a later phase before the current one's exit gate is met.
