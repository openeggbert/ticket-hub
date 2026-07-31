# Ticket Hub V1 implementation roadmap (reduced scope)

This supersedes `ROADMAP.md` (14 target phases) as the day-to-day build plan. `ROADMAP.md` remains for
post-V1 reference. Every phase below still ends with a **compiling, migratable, tested product** — that
rule from the original roadmap is unchanged. No phase leaves a half-integrated migration or a knowingly
broken backend. Both PostgreSQL and SQLite build configurations stay green at every phase exit gate
(D137).

Phase 0 (baseline/governance) is already done — see `handoff/IMPLEMENTATION_STATE.md`. Phases below start
from the current 0.2.0 prototype state.

## Phase 1 — Identity and sessions (Milestone 1)

**2026-07-31 further reduction** (agreed with the product owner before implementation started, applies
only to *sequencing* within V1 — nothing here removes a V1 feature outright except the last bullet):

- No `handle` column yet — added in Phase 4 when @mentions first need it (D56 is still V1 scope, just
  resequenced).
- Login attempt counter only; no configurable lockout window/policy — the full version rides along with
  REST rate limiting in Phase 6 (D124).
- No active-session list / "sign out everywhere" endpoint yet — added in Phase 6 alongside PAT/account
  management work (D54 is still V1 scope, just resequenced).
- Admin sets a user's password directly; **no `must_change_password` flag or forced-change-on-first-login
  flow** — this is a permanent V1 simplification, not a resequencing.

Phase 1 scope, reduced accordingly:

- Introduce `Principal` and remove the fixed `demo` user entirely (no dual demo-mode toggle) from write
  use cases.
- Users table migrates to UUID + unique email (no `handle` yet); `local_credentials` with Argon2id
  (D53).
- Administrator-created accounts only — an admin action creates a user with a password set directly by
  the admin (D2). No invitation flow, no OIDC, no public registration.
- Server-side cookie sessions with CSRF, HttpOnly/Secure/SameSite, single-session logout (D54, session
  list deferred to Phase 6 as noted above).
- Admin-performed password reset (sets a new password directly, no forced-change flow) (D53).

**Exit gate:** no fixed demo identity anywhere in the codebase; login/logout/session endpoints tested on
both databases.

## Phase 2 — Authorization and projects (Milestone 1)

- Fixed project roles (Admin/Member/Viewer) enforced via `project_members.role_key` (D3).
- Global administrator flag on `users`.
- Project CRUD, archive (read-only enforcement on the existing `archived` column), recycle bin with
  fixed 90-day on-demand retention (no background job) (D87, D89).
- Anonymous read-only access toggle, disabled by default (D59).
- Project visibility: all authenticated users see all projects; roles gate writes only (D58).

**Exit gate:** every route is explicitly public, authenticated, or role-checked; no fixed demo user
remains anywhere.

## Phase 3 — Issue core and fixed workflow (Milestone 1)

- Fixed issue types/priorities/resolutions (already implemented) wired to real principals.
- Fixed statuses and the one hardcoded workflow, including the fixed sub-task/resolution/reopen rules
  (D4, D68-D70).
- Full issue create/read/edit with optimistic locking (`version`, 409 on conflict, UI reload/reapply)
  (D129) — build on the existing partial implementation.
- Epic/Sub-task hierarchy enforcement, "No Epic" backlog group (D64-D66).
- Labels (already implemented), simple integer rank with renumber-on-insert (D31). **Known conflict
  with existing code:** migration `003_product_foundation.sql` already added `issues.rank_value` as
  `TEXT DEFAULT 'm'`, anticipating a LexoRank-style value. Since applied migrations are immutable, do
  not edit it — add a new migration in this phase that introduces the integer ordering column (and
  either drops or repurposes the now-unused text column) instead.
- Recycle bin and permanent key-alias reservation for issues (existing foundation).
- Simple cloning (D60); fixed issue-link catalog (D17); watchers (self-only, D20); voting (D79).
- Simple bulk actions: multi-select + one action + confirm (D36); always-allowed project moves (D37).

**Exit gate:** all issue mutations are transactional and append structured history; fixed workflow
rules cannot be bypassed from any route.

## Phase 4 — Collaboration (Milestone 2)

- Full Markdown editor with toolbar/preview (already partly implemented, D16); sanitized rendering.
- Comments: edited-flag (no version history, D81), tombstone delete (existing soft-delete columns,
  D82), simplified own/admin edit-delete permissions (D83), fixed emoji reactions (D84).
- Mentions with `@handle` autocomplete plus in-app notification (D80).
- Story points field (already implemented, D12); simplified worklogs (D13).
- Fixed in-app notification set: assigned-to-me, mentioned, comment on watched issue (D14).
- Simple append-only admin/security audit log (D23).

**Exit gate:** comment/mention/notification flows fully tested on both databases; no email dependency
anywhere in this phase.

## Phase 5 — Attachments and Kanban board (Milestone 2)

- Local filesystem attachment storage, hardwired (D15); fixed size/count/extension limits (D98);
  upload-time SHA-256 verification only (D105).
- All 4 native-element previews: image, PDF, text, audio/video (D99).
- Full upload + drag/drop + paste in the Markdown editor (D100).
- Sortable attachment list + recycle bin, fixed 90-day on-demand retention (D101, D102).
- One auto-created Kanban board per project; columns = fixed statuses; soft WIP limits (D32, D33).
- Simple `LIKE`/`ILIKE` search across summary/description, ad-hoc UI filters only, no persistence
  (D10, D43).
- Fixed personal dashboard (assigned/watched/recent activity/deadlines/simple stats — no sprint widget)
  (D24).

**Exit gate:** attachments and board usable end-to-end on a real project; dashboard reflects live data
on both databases.

## Phase 6 — API, security hardening, and export (Milestone 3)

- `/api/v1` REST surface, PAT-authenticated only (hashed token, expiry, revocation, last-used — no
  scopes/rotation/service accounts) (D39, D40).
- Fixed rate limits (login + write endpoints, per IP/user) (D124), including the full account-lockout
  policy resequenced from Phase 1; fixed request/body/batch-size constants (D125); numbered/offset
  pagination (D126).
- Active-session list and "sign out everywhere" endpoint, resequenced from Phase 1 (D54).
- Read-only CSV export of issues (D48).
- Security hardening pass: dependency review, header review, session/CSRF review against
  `handoff/KNOWN_CONSTRAINTS_AND_RISKS.md`.

**Exit gate:** `/api/v1` covers the same operations as the web UI for issues/comments/projects; rate
limiting and PAT auth tested on both databases.

## Phase 7 — Backup, restore, upgrade, and observability (Milestone 3)

- Backup: attachment-directory copy + database dump, offline/maintenance-window only, no manifest, no
  online snapshot (D106, D107).
- Restore: direct `ticket-hub restore` into the target database with a confirmation warning; admin is
  responsible for their own pre-restore backup (D108).
- Upgrade: `ticket-hub migrate` remains the entire upgrade mechanism (D111).
- In-app admin banner for available new versions (D112).
- Structured JSON logs to stdout (D133).

**Exit gate:** a fresh install → seed → backup → restore cycle is scripted and tested on both databases;
`ctest --output-on-failure` green.

## Phase 8 — Packaging and release hardening (Milestone 4)

- Official Docker image + Docker Compose (with PostgreSQL) as the only distribution path (D50).
- Baseline accessibility pass (semantic HTML, keyboard operability — no formal WCAG audit deliverable)
  (D47).
- Light/dark theme finalized, no high-contrast/branding (D46).
- Manual smoke test across the two most recent versions of Chrome/Firefox/Edge/Safari (D139).
- Documentation pass: `README.md`, `NEXT.md`, `CHANGELOG.md`, `docs/SCHEMA.md`, `docs/SCOPE.md`,
  `docs/VERIFICATION.md` all current.
- Threat-model / security self-review of what V1 actually built (not the original full-scope plan).

**Exit gate:** `docker compose up` produces a usable, documented V1 instance; all supported build
configurations (SQLite+PostgreSQL core, SQLite-only, PostgreSQL-only, server target) compile and pass
tests; no known open security issue from the hardening pass.

## Milestones at a glance

| Milestone | Phases | What it delivers |
|---|---|---|
| **1 — Minimal usable tracker** | 1-3 | Login, projects, roles, issues, fixed workflow, hierarchy |
| **2 — Daily personal/team use** | 4-5 | Comments, mentions, attachments, Kanban board, dashboard |
| **3 — Public beta** | 6-7 | REST API, CSV export, backup/restore, upgrade path |
| **4 — Production hardening** | 8 | Docker packaging, accessibility/security passes, docs |

See `docs/IMPLEMENTATION_ESTIMATE.md` for Claude Code hour ranges per milestone.

## No removed feature appears in this roadmap

Every deliverable listed above traces to a `KEEP_FOR_V1`, `SIMPLIFY_FOR_V1`, or
`ALREADY_IMPLEMENTED_AND_KEEP` decision in `docs/REDUCED_SCOPE_DECISIONS.md`. Nothing classified
`DEFER_AFTER_V1` or `REMOVE_COMPLETELY` (see `docs/REMOVED_AND_DEFERRED_FEATURES.md`) appears in any
phase's deliverables or exit gate.
