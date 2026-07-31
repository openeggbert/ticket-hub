# Ticket Hub next work

Current version: 0.2.0 (Phase 3 partially complete at the core layer, server target still unverified)
Current roadmap: **reduced-scope V1** — see `REDUCED_SCOPE_SPECIFICATION.md` and
`docs/REDUCED_SCOPE_ROADMAP.md`. `SPECIFICATION.md` and `docs/ROADMAP.md` are kept as the long-term
aspirational baseline but are **not** the current build target.

Scope was re-reviewed decision-by-decision with the product owner on 2026-07-31 (142/142 decisions;
see `docs/REDUCED_SCOPE_DECISIONS.md` and `docs/REMOVED_AND_DEFERRED_FEATURES.md`). Do not implement
anything from the removed/deferred list without an explicit new product conversation.

## Completed so far

- Approved reduced-scope V1 specification, architecture, data model, roadmap, and effort estimate.
- Prior batch: ordered checksummed migrations with PostgreSQL advisory locking, separate demo seed,
  issue optimistic-lock version and HTTP conflict foundation, project/issue key alias and recycle-bin
  schema foundations, key/label normalization, administration CLI (version/diagnostics/migrate/
  seed-demo), domain/migration/SQLite integration tests, SQLite-only and PostgreSQL-only build
  verification.
- **Phase 1 (identity and sessions):** `Principal`, `AuthService` (login/logout/session validation,
  administrator-only `createUser`, minimal login-attempt lockout), Argon2id password hashing, SHA-256
  session-token hashing, migration `004_identity.sql` on both backends, `ticket-hub-cli create-user`.
  The fixed `demo` user is gone from every write path.
- **Phase 2 (authorization and projects):** fixed project roles (`Domain::ProjectRoleViewer`/`Member`/
  `Admin`, `Domain::projectRoleRank`) and a global-administrator bypass, enforced in `TicketService`;
  `createIssue`/`changeStatus`/`addComment` require project-Member-or-above. Project lifecycle
  (`createProject`/`setProjectArchived`/`deleteProject`/`restoreProject`/`listDeletedProjects`/
  `permanentlyDeleteProject`) with the fixed 90-day on-demand recycle-bin retention. Migration
  `005_authorization.sql` adds `installation_settings`. Installation-wide anonymous read-access toggle
  (D59, off by default) — every read use case takes `std::optional<Principal>`.
- **Phase 3, partial (fixed workflow and hierarchy):** the fixed Epic → Story/Task/Bug → Sub-task
  hierarchy (D5, D29, D64-D66), enforced by `TicketService::requireValidHierarchy` on issue creation
  (`CreateIssueRequest` gained `parentIssueKey`, now actually persisted to `issues.parent_issue_id`,
  which previously existed and was read but never written). The fixed workflow's hardcoded transition
  rules (D68-D70), enforced transactionally inside `IDatabase::changeIssueStatus` in both adapters (not
  the application layer -- these rules depend on current database state and must not race with a
  concurrent change): completing an issue requires a valid `resolution` and is rejected with the new
  `Domain::WorkflowViolation` (HTTP 422) while any sub-task is unfinished; reopening (leaving a
  Done-category status) always clears `resolution` and never touches child issues. `Domain::Issue`
  gained a `resolution` field.
- **Phase 3, partial continued (full issue edit), this batch:** `Domain::EditIssueRequest` and
  `IDatabase::editIssue`/`TicketService::editIssue` (D129) -- a full-replacement edit of summary,
  description, priority, assignee, story points, due date, and labels, sharing `changeIssueStatus`'s
  optimistic-locking contract (`expectedVersion` -> `Domain::ConcurrencyConflict`) and the same
  project-Member-or-above role requirement. One `issue_history` row per field that actually changed.
  Does not edit `issueTypeKey`/`parentIssueKey` -- re-typing/re-parenting is not yet implemented. Shared
  validation logic factored into `appendIssueContentErrors` (`Validation.cpp`) and `normalizeLabels`
  (`TicketService.cpp`) instead of duplicating it between create and edit.
- Tested: `ctest --output-on-failure` is 7/7 green (`domain`, `migration`, `sqlite-integration`,
  `identity`, `authorization`, `workflow`, `crypto`) on SQLite, in all three build configurations (full,
  SQLite-only, PostgreSQL-only). Every Phase 2/3 core-layer addition was additionally verified manually
  against a live local PostgreSQL server (created and dropped for each batch's verification). Full
  detail in `docs/VERIFICATION.md`.
- Web layer source (`src/web/Api.cpp`, `HttpServer.cpp`, `main.cpp`) updated to match all phases so far:
  Phase 1's `/api/auth/login|logout|me` and session-cookie/CSRF protection, Phase 2's project CRUD
  routes and every read route resolving an optional `Principal`, Phase 3's `parentIssueKey` on issue
  creation, `resolution` on status changes, `Domain::WorkflowViolation` mapped to HTTP 422, and the new
  `PATCH /api/issues/{key}` full-edit route. While adding the edit route, fixed a real bug found by
  inspection: three existing routes (`POST /api/issues`, `PATCH /api/issues/{key}/status`,
  `POST /api/issues/{key}/comments`) were missing a `catch (const Domain::Forbidden&)` handler, so a
  project-role authorization failure would have fallen through to the generic 500 handler instead of
  403. **None of `Api.cpp` has been compiled** — Crow is unavailable in this sandbox (network to
  `github.com` blocked). See "Known verification limitation" below; this is still the actual next thing
  to close out, now covering three phases' worth of route changes.

## Immediate next step: verify the server target

No phase's exit gate is fully closed until this is done — it has been deferred across all three phases
for the same environment reason, not skipped:

1. In an environment with network access to `github.com` (or a preinstalled/vendored Crow 1.3.3), build
   the `ticket-hub` server target (`-DTICKETHUB_BUILD_SERVER=ON`) and fix any compile errors in
   `src/web/Api.cpp` / `HttpServer.cpp` / `main.cpp` — they were written carefully against the existing
   patterns but never compiled.
2. Smoke-test Phase 1 end-to-end: `create-user` → `POST /api/auth/login` → confirm `Set-Cookie` headers
   for `th_session` (HttpOnly) and `th_csrf` (readable) → `GET /api/auth/me` → `POST /api/issues` with
   and without the `X-CSRF-Token` header (expect 201 vs. 403) → `POST /api/auth/logout`.
3. Smoke-test Phase 2 end-to-end: `POST /api/projects` as a non-admin (expect 403) and as an admin
   (expect 201); the archive/delete/restore/permanent-delete/recycle-bin-list routes each as
   project-admin/global-admin/neither; the anonymous-read-access toggle end-to-end.
4. Smoke-test Phase 3 end-to-end: `POST /api/issues` with a `parentIssueKey` violating each hierarchy
   rule (expect 400) and satisfying it (expect 201); `PATCH /api/issues/{key}/status` to a Done-category
   status without `resolution` (expect 422), with an unknown `resolution` (expect 422), with a valid one
   (expect 200 and the resolution present in the response); attempt to complete a parent with an
   unfinished sub-task (expect 422); reopen a completed issue and confirm `resolution` is null in the
   response; `PATCH /api/issues/{key}` as a non-member (expect 403, confirming the just-fixed
   `Domain::Forbidden` catch actually works end-to-end) and as a member with a stale `expectedVersion`
   (expect 409) and with a fresh one (expect 200, every field updated).
5. Add a minimal login page (and, ideally, project-management and hierarchy/resolution UI) to `web/`
   (there isn't one yet) so the demo UI can actually authenticate and exercise the newer routes instead
   of hitting 401s/blank forms once the session check is live.
6. Only then close all three phases' exit gates for real.

## After the server target is verified: finish Phase 3, then continue the roadmap

Phase 3 is only partially done. Still open, in `docs/REDUCED_SCOPE_ROADMAP.md`'s order:

- Re-typing (`issueTypeKey`) or re-parenting (`parentIssueKey`) an issue after creation --
  `TicketService::editIssue` deliberately does not touch either field yet.
- Simple cloning (D60): field-copy clone into a new issue in the same project, creating a
  `clones`/`is cloned by` link.
- Fixed issue-link catalog (D17): blocks/is blocked by, relates to, duplicates/is duplicated by (clones
  covered by cloning above). `issue_links` table already exists as a schema foundation.
- Self-only watchers (D20) and voting (D79) — no schema yet for either.
- Simple bulk actions (D36: multi-select + one action + confirm) and always-allowed project moves (D37).
- The integer rank/renumber migration (D31), which needs a **new** migration (not an edit to
  `003_product_foundation.sql`, which is already applied and immutable) to introduce the real ordering
  column and retire the unused `issues.rank_value` text column that anticipated a different (LexoRank)
  design.

After Phase 3 is fully closed, continue with Milestone 2 (collaboration, attachments, Kanban board),
Milestone 3 (API, backup/restore), Milestone 4 (packaging and hardening). Do not jump ahead to
later-phase features early, and do not implement anything from `docs/REMOVED_AND_DEFERRED_FEATURES.md`.

## Known verification limitation

The `ticket-hub` server target (Crow) could not be compiled in this sandbox because outbound access to
`github.com` — needed for CMake `FetchContent` to fetch Crow — was blocked by the session's network
egress policy. This is the same limitation recorded in every prior session, now spanning three phases of
route changes. Core, CLI, and all seven test binaries compile and pass on both SQLite and PostgreSQL, in
every supported build configuration. Full detail, including exactly what was and was not verified, is in
`docs/VERIFICATION.md`.
