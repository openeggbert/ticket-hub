# Ticket Hub next work

Current version: 0.2.0 (Phase 1 in progress)
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
- **Phase 1, core layer (this batch):** `Principal`, `AuthService` (login/logout/session validation,
  administrator-only `createUser`, minimal login-attempt lockout), Argon2id password hashing
  (`common/PasswordHash`), SHA-256 session-token hashing (`common/Sha256`), migration
  `004_identity.sql` on both backends (`local_credentials`, `sessions`, drops `users.username`, adds
  `time_zone`/`clock_format`/`is_admin`), `ticket-hub-cli create-user`. The fixed `demo` user is gone
  from every write path; `TicketService::createIssue`/`changeStatus`/`addComment` now require a real
  `Domain::Principal`. All further-reduced per `docs/REDUCED_SCOPE_ROADMAP.md` Phase 1 (no `handle`
  column yet, no active-session list yet, no forced-password-change flow ever).
- Tested: `ctest --output-on-failure` is 5/5 green (including two new suites, `crypto_tests` and
  `identity_integration_tests`) on both SQLite (automated) and PostgreSQL (manually verified against a
  live local server — migrate/seed/create-user/login/validate-session/logout all confirmed working).
  Full detail in `docs/VERIFICATION.md`.
- Web layer source (`src/web/Api.cpp`, `HttpServer.cpp`, `main.cpp`) updated to match: new
  `/api/auth/login|logout|me` routes, session-cookie + double-submit-CSRF protection on existing write
  routes, `assigneeUsername` renamed to `assigneeEmail` (also updated in `web/index.html`/`app.js`).
  **This has not been compiled** — Crow is unavailable in this sandbox (network to `github.com`
  blocked). See "Known verification limitation" below; this is the actual next thing to close out.

## Immediate next step: verify the server target

Phase 1 is not done until this is closed:

1. In an environment with network access to `github.com` (or a preinstalled/vendored Crow 1.3.3), build
   the `ticket-hub` server target (`-DTICKETHUB_BUILD_SERVER=ON`) and fix any compile errors in
   `src/web/Api.cpp` / `HttpServer.cpp` / `main.cpp` — they were written carefully against the existing
   patterns but never compiled.
2. Smoke-test end-to-end by hand: `create-user` → `POST /api/auth/login` → confirm `Set-Cookie` headers
   for `th_session` (HttpOnly) and `th_csrf` (readable) → `GET /api/auth/me` → `POST /api/issues` with
   and without the `X-CSRF-Token` header (expect 201 vs. 403) → `POST /api/auth/logout` → confirm the
   session cookie no longer authenticates.
3. Add a minimal login page to `web/` (there isn't one yet) so the demo UI can actually authenticate
   instead of hitting 401s on every write once the session check is live.
4. Only then close Phase 1's exit gate for real: "no fixed demo identity remains anywhere in the
   codebase; login/logout/session endpoints are tested on both databases."

## After the server target is verified: Phase 2

Continue in order through `docs/REDUCED_SCOPE_ROADMAP.md`: Phase 2 (authorization and projects — fixed
project roles enforced via `project_members.role_key`, project archive/recycle-bin UI, anonymous
read-access toggle), Phase 3 (issue core and the fixed workflow), then Milestone 2 (collaboration,
attachments, Kanban board), Milestone 3 (API, backup/restore), Milestone 4 (packaging and hardening).
Do not jump ahead to later-phase features early, and do not implement anything from
`docs/REMOVED_AND_DEFERRED_FEATURES.md`.

Note: Phase 2's authorization *logic* (fixed-role checks in `TicketService`/a new
`AuthorizationService`) does not strictly require Crow either and could be started in parallel with the
server-target verification above if useful — but the exit gate for "every route is explicitly public,
authenticated, or role-checked" still needs the real HTTP layer compiling to actually verify.

## Known verification limitation

The `ticket-hub` server target (Crow) could not be compiled in this sandbox because outbound access to
`github.com` — needed for CMake `FetchContent` to fetch Crow — was blocked by the session's network
egress policy. This is the same limitation recorded in every prior session. Core, CLI, and all five test
binaries compile and pass on both SQLite and PostgreSQL. Full detail, including exactly what was and
was not verified, is in `docs/VERIFICATION.md`.
