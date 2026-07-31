# Ticket Hub next work

Current version: 0.2.0 (Phase 2 core layer complete, server target still unverified)
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
  administrator-only `createUser`, minimal login-attempt lockout), Argon2id password hashing
  (`common/PasswordHash`), SHA-256 session-token hashing (`common/Sha256`), migration
  `004_identity.sql` on both backends (`local_credentials`, `sessions`, drops `users.username`, adds
  `time_zone`/`clock_format`/`is_admin`), `ticket-hub-cli create-user`. The fixed `demo` user is gone
  from every write path.
- **Phase 2 (authorization and projects, this batch), core layer:** fixed project roles
  (`Domain::ProjectRoleViewer`/`Member`/`Admin`, `Domain::projectRoleRank`) and a global-administrator
  bypass, enforced in `TicketService` (`requireProjectRole`/`requireGlobalAdmin`, throwing
  `Domain::Forbidden`); `createIssue`/`changeStatus`/`addComment` now require project-Member-or-above.
  Project lifecycle: `createProject` (global admin), `setProjectArchived` (project admin),
  `deleteProject`/`restoreProject`/`listDeletedProjects`/`permanentlyDeleteProject` (soft-delete by
  project admin, everything else recycle-bin-related is global-admin-only per D88). Migration
  `005_authorization.sql` (both backends) adds `installation_settings`. Installation-wide anonymous
  read-access toggle (D59, off by default): every read use case (`listProjects`, `listIssues`,
  `findIssue`, `listComments`, `dashboard`) now takes `std::optional<Principal>` and rejects an
  anonymous caller with `Domain::AuthenticationRequired` unless the toggle is on; any authenticated
  caller can always read (D58: roles gate writes only).
- Tested: `ctest --output-on-failure` is 6/6 green (`domain`, `migration`, `sqlite-integration`,
  `identity`, the new `authorization-tests`, `crypto`) on SQLite (automated); the Phase 2
  authorization/project-lifecycle/settings code paths were additionally verified manually against a
  live local PostgreSQL server (created and dropped for this batch's verification). Full detail in
  `docs/VERIFICATION.md`.
- Web layer source (`src/web/Api.cpp`, `HttpServer.cpp`, `main.cpp`) updated to match both phases: Phase
  1's `/api/auth/login|logout|me` and session-cookie/CSRF protection, and Phase 2's project CRUD routes
  (`POST/PATCH/DELETE /api/projects/...`, `GET /api/projects/deleted`,
  `GET/PUT /api/settings/anonymous-read`) plus every existing read route now resolving an optional
  `Principal`. **None of this has been compiled** — Crow is unavailable in this sandbox (network to
  `github.com` blocked). See "Known verification limitation" below; this is still the actual next thing
  to close out, now covering two phases' worth of route changes instead of one.

## Immediate next step: verify the server target

Neither Phase 1 nor Phase 2 is fully done until this is closed — it has been deferred across both
phases for the same environment reason, not skipped:

1. In an environment with network access to `github.com` (or a preinstalled/vendored Crow 1.3.3), build
   the `ticket-hub` server target (`-DTICKETHUB_BUILD_SERVER=ON`) and fix any compile errors in
   `src/web/Api.cpp` / `HttpServer.cpp` / `main.cpp` — they were written carefully against the existing
   patterns but never compiled.
2. Smoke-test Phase 1 end-to-end by hand: `create-user` → `POST /api/auth/login` → confirm `Set-Cookie`
   headers for `th_session` (HttpOnly) and `th_csrf` (readable) → `GET /api/auth/me` → `POST /api/issues`
   with and without the `X-CSRF-Token` header (expect 201 vs. 403) → `POST /api/auth/logout` → confirm
   the session cookie no longer authenticates.
3. Smoke-test Phase 2 end-to-end: `POST /api/projects` as a non-admin (expect 403) and as an admin
   (expect 201); `PATCH /api/projects/{key}/archived`, `DELETE /api/projects/{key}`,
   `GET /api/projects/deleted`, `POST /api/projects/{key}/restore`,
   `DELETE /api/projects/{key}/permanent` each as project-admin/global-admin/neither; `GET`/`PUT
   /api/settings/anonymous-read`; confirm an anonymous `GET /api/issues` returns 401 by default and 200
   once the toggle is flipped on.
4. Add a minimal login page (and, ideally, a project-management view) to `web/` (there isn't one yet) so
   the demo UI can actually authenticate instead of hitting 401s on every write once the session check
   is live.
5. Only then close both phases' exit gates for real: "no fixed demo identity remains anywhere in the
   codebase; login/logout/session endpoints are tested on both databases" (Phase 1) and "every route is
   explicitly public, authenticated, or role-checked; no fixed demo user remains anywhere" (Phase 2).

## After the server target is verified: Phase 3

Continue in order through `docs/REDUCED_SCOPE_ROADMAP.md`: Phase 3 (issue core and the fixed workflow —
fixed statuses/transitions, Epic/Sub-task hierarchy, full issue edit with optimistic locking already
partially in place), then Milestone 2 (collaboration, attachments, Kanban board), Milestone 3 (API,
backup/restore), Milestone 4 (packaging and hardening). Do not jump ahead to later-phase features early,
and do not implement anything from `docs/REMOVED_AND_DEFERRED_FEATURES.md`.

## Known verification limitation

The `ticket-hub` server target (Crow) could not be compiled in this sandbox because outbound access to
`github.com` — needed for CMake `FetchContent` to fetch Crow — was blocked by the session's network
egress policy. This is the same limitation recorded in every prior session, now spanning two phases of
route changes. Core, CLI, and all six test binaries compile and pass on both SQLite and PostgreSQL. Full
detail, including exactly what was and was not verified, is in `docs/VERIFICATION.md`.
