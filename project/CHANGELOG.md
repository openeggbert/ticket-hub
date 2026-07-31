# Changelog

## Unreleased — Phase 2: authorization and projects (reduced scope)

- Added fixed project roles (`Domain::ProjectRoleViewer`/`Member`/`Admin`, `Domain::projectRoleRank`) and
  a global-administrator bypass, enforced in `TicketService` via new `requireProjectRole`/
  `requireGlobalAdmin` helpers that throw `Domain::Forbidden` (HTTP 403). `createIssue`, `changeStatus`,
  and `addComment` now require Member-or-above on the issue's project.
- Added project lifecycle to `IDatabase`/`SqliteDatabase`/`PostgresDatabase` and wrapped it in
  `TicketService`: `createProject` (global admin), `setProjectArchived` (project admin), `deleteProject`
  (project admin, soft delete to the recycle bin), `restoreProject`/`listDeletedProjects`/
  `permanentlyDeleteProject` (global admin only, per D88's "admin restore or permanent delete"). The
  recycle bin purges anything past the fixed 90-day retention on access; there is no background job
  (D89, D51).
- Added migration `005_authorization.sql` (both backends): `installation_settings` generic key/value
  table for the small number of remaining installation-level toggles.
- Added the installation-wide anonymous read-access toggle (D59, off by default,
  `TicketService::isAnonymousReadEnabled`/`setAnonymousReadEnabled`). Every read use case
  (`listProjects`, `listIssues`, `findIssue`, `listComments`, `dashboard`) now takes an
  `std::optional<Principal>`; an anonymous caller is rejected with the new `Domain::AuthenticationRequired`
  (HTTP 401) unless the toggle is on. Any authenticated user still sees all projects regardless of
  membership — roles gate writes only (D58).
- Added `POST /api/projects`, `PATCH /api/projects/{key}/archived`, `DELETE /api/projects/{key}`,
  `GET /api/projects/deleted`, `POST /api/projects/{key}/restore`, `DELETE /api/projects/{key}/permanent`,
  and `GET`/`PUT /api/settings/anonymous-read` to `src/web/Api.cpp`; updated every existing read route to
  resolve an optional `Principal` and pass it through (**not yet compiled** — see "Known verification
  limitation" in `README.md`).
- Added `authorization_integration_tests` (SQLite): non-member vs. member vs. project-admin issue writes,
  not-found semantics under authorization, the anonymous-read-access toggle, and the full project
  lifecycle authorization matrix (project-admin vs. global-admin-only actions).
- Manually verified `createProject`, `setProjectArchived`, `softDeleteProject`, `listDeletedProjects`
  (`LATERAL` join + on-demand purge), `restoreProject`, `permanentlyDeleteProject`, and the
  `installation_settings` get/set-with-upsert methods against a live local PostgreSQL 16 server.

## Phase 1: identity and sessions (reduced scope)

- Re-reviewed all 142 original product decisions with the product owner and produced a reduced V1
  scope (`REDUCED_SCOPE_SPECIFICATION.md` and friends); this is now the build target instead of the
  original full-scope plan.
- Added local-account identity: `Principal`, Argon2id password hashing (`common/PasswordHash`), SHA-256
  session-token hashing (`common/Sha256`), and `AuthService` (login/logout/session validation,
  administrator-only account creation, minimal login-attempt lockout).
- Added migration `004_identity.sql` (both backends): `local_credentials`, `sessions`, and `users` gains
  `time_zone`/`clock_format`/`is_admin` while losing the prototype's `username` column. The SQLite
  variant rebuilds the `users` table (SQLite cannot `DROP COLUMN` a column in a `UNIQUE` constraint
  directly); `SqliteDatabase::migrate()` now disables and re-verifies foreign keys around every
  migration to support this and future table-rebuild migrations safely.
- Removed the fixed `demo` user from every write path. `TicketService::createIssue`/`changeStatus`/
  `addComment` now require an explicit `Domain::Principal` argument.
- Added `ticket-hub-cli create-user <email> <displayName> <password> [--admin]` — the entire V1
  registration/reset story (no public registration, no invitations, no forced password change).
- `CreateIssueRequest::assigneeUsername` renamed to `assigneeEmail` (assignee lookup is now by email,
  not the removed username column); updated in `web/index.html` and `web/app.js` accordingly.
- Added `/api/auth/login`, `/api/auth/logout`, `/api/auth/me`, and session-cookie + double-submit-CSRF
  protection to the existing write routes in `src/web/Api.cpp` (**not yet compiled** — see
  "Known verification limitation" in `README.md`).
- Added `crypto_tests` (SHA-256 known-answer vectors, Argon2id round-trip) and
  `identity_integration_tests` (SQLite: create-user, login success/failure, anti-enumeration, lockout,
  session lifecycle) test binaries; all previously-passing tests continue to pass.
- Manually verified migrations, seeding, `create-user`, and a full login/session/logout cycle against a
  live local PostgreSQL 16 server, in addition to the automated SQLite test suite.

## 0.2.0 — product baseline and migration foundation

- Consolidated the approved Jira-like product specification.
- Added architecture, target data model and phased roadmap documents.
- Added ordered schema migration discovery and checksum verification.
- Added PostgreSQL advisory locking for migrations.
- Separated demo seed execution from schema migration history.
- Added issue version to support optimistic status-change conflicts.
- Added project/issue key-alias tables and alias lookup for issues.
- Added recycle-bin foundation columns for projects, issues, comments and attachments.
- Added normalized project/issue key and label handling.
- Fixed duplicate project rows in the SQLite adapter.
- Expanded migration, domain and SQLite integration tests.
