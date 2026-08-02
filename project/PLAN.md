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
- **Milestone 3** (Phase 6: API/security hardening/export; Phase 7: backup/restore/upgrade) -- **started**.
  Done so far: personal access tokens (D39/D40, self-service create/list/revoke, `Authorization: Bearer`
  wired into every route); the active-session list/"sign out everywhere" endpoint (D54); fixed rate
  limits (D124/D125, `TicketHub::Web::RateLimiter` -- 20 login attempts/IP/15min, 120 writes/min/
  user-or-IP, both with a `Retry-After` header); the versioned `/api/v1` prefix (D127, every route except
  `GET /api/health`); read-only CSV export of issues (D48, `GET /api/v1/issues/export.csv` plus a web UI
  link); and fixed request-body/bulk-item constants (D125, 1 MiB JSON body cap, 200-item bulk cap). Still
  open: numbered pagination (D126, which also unblocks D125's undefined "max page size"), the security
  hardening pass, and all of Phase 7. No web UI yet for managing tokens or sessions.
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
