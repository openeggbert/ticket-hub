# Ticket Hub

Ticket Hub is a self-hosted Jira-like Software issue tracker written in C++20. It uses Crow for HTTP, vanilla HTML/CSS/JavaScript for the web interface, PostgreSQL as the primary production database and SQLite as a smaller single-process backend.

License: MIT.  
Main namespace: `TicketHub`.

## Project status

The current build target is the **reduced-scope V1** (see `REDUCED_SCOPE_SPECIFICATION.md`), not the
original full Jira-like plan in `SPECIFICATION.md`, which remains only as a long-term aspirational
reference. It is **not yet production-ready**: the Kanban board and attachments are future phases
(`docs/REDUCED_SCOPE_ROADMAP.md`).

Implemented now:

- projects and transactional project-local issue keys,
- issue list/detail/create, status changes, labels and comments,
- a simple demo Kanban UI (not yet identity-aware),
- **local accounts: Argon2id password hashing, server-side sessions, minimal login-attempt lockout**
  (`AuthService` — Phase 1 of `docs/REDUCED_SCOPE_ROADMAP.md`),
- **administrator-only account creation via `ticket-hub-cli create-user`** — there is no
  self-registration or invitation flow in V1,
- **fixed project roles (Viewer/Member/Admin) and a global administrator flag, enforced on every issue
  and project write** (`TicketService::requireProjectRole`/`requireGlobalAdmin` — Phase 2 of
  `docs/REDUCED_SCOPE_ROADMAP.md`),
- **project lifecycle: create (global admin), archive/unarchive (project admin), recycle bin with fixed
  90-day on-demand retention, restore, and permanent delete (global admin)**,
- **installation-wide anonymous read-access toggle, off by default** — every read use case takes an
  optional `Principal`; an anonymous caller is rejected unless the toggle is on,
- **fixed Epic → Story/Task/Bug → Sub-task hierarchy, enforced on issue creation** (`TicketService::
  requireValidHierarchy` — Phase 3 of `docs/REDUCED_SCOPE_ROADMAP.md`, partial): a Sub-task requires a
  same-project Story/Task/Bug parent, an Epic may not have a parent, an optional Story/Task/Bug parent
  must be an Epic,
- **the fixed workflow's hardcoded transition rules, enforced transactionally in `changeIssueStatus`**:
  a resolution is required to complete an issue and is cleared automatically on reopen, and an issue
  cannot complete while it has an unfinished sub-task,
- **full-replacement issue edit with optimistic locking** (D129): summary, description, priority,
  assignee, story points, due date and labels, sharing the same `expectedVersion`/409 contract as status
  changes, with one `issue_history` row per field that actually changed,
- **the fixed issue-link catalog** (D17): `blocks`/`relates_to`/`duplicates`/`clones`, each visible from
  both linked issues with the correct outward/inward label; creating or deleting a link requires access
  to both projects,
- **simple field-copy cloning** (D60): summary/description/type/priority/labels copied into a new issue,
  with an automatic `clones` link back to the original,
- **self-service watching and voting** (D20/D79): any authenticated user may watch or vote on any issue
  — the one write with no project-role requirement — idempotent on repeat, with a visible watcher/voter
  list,
- **issue recycle bin** (D22): soft delete (project admin), restore/list/permanent delete (global admin
  only), fixed 90-day on-demand retention — mirrors the project recycle bin exactly,
- **simple bulk actions** (D36): status/assignee/label/recycle applied to a list of issue keys, each
  through the same single-issue operation and authorization as doing it one at a time; a partial failure
  is reported, not rolled back,
- **simple integer manual ordering with renumbering** (D31): a per-project `rank_order`, replacing the
  never-used LexoRank-style `rank_value` placeholder; moving an issue renumbers the whole project's issue
  list in one pass rather than using a minimal-diff/fractional scheme,
- **moving an issue to a different project** (D37): no compatibility check is needed since every project
  shares the same fixed types/workflow/fields — a move is a `project_id` change plus a freshly allocated
  key/number, exactly like creating a new issue there; rejected if the issue has a parent or any children;
  requires project-Member-or-above on both the source and target projects,
- every issue/comment/project write now takes an explicit `Principal` instead of a fixed demo user,
- PostgreSQL and SQLite adapters,
- ordered schema migration discovery with stored checksums,
- PostgreSQL migration advisory lock,
- issue optimistic-lock versioning for status updates,
- permanent project key-alias schema foundation, and a permanent issue key-alias mechanism actually
  written to by `moveIssue` (D38): the vacated key stays permanently resolvable to the moved issue,
- recycle-bin schema foundations and live-query filtering,
- domain, migration, crypto, SQLite integration, identity, authorization, and workflow tests (see "Known
  verification limitation" below for what is *not* yet compiled/tested in this environment).

Authoritative documents:

- [REDUCED_SCOPE_SPECIFICATION.md](REDUCED_SCOPE_SPECIFICATION.md) — the current V1 product baseline,
- [docs/REDUCED_SCOPE_DECISIONS.md](docs/REDUCED_SCOPE_DECISIONS.md) — the current V1 decision register,
- [docs/REMOVED_AND_DEFERRED_FEATURES.md](docs/REMOVED_AND_DEFERRED_FEATURES.md) — what is intentionally not being built,
- [docs/REDUCED_SCOPE_ROADMAP.md](docs/REDUCED_SCOPE_ROADMAP.md) — the current phased implementation plan,
- [docs/REDUCED_SCOPE_ARCHITECTURE.md](docs/REDUCED_SCOPE_ARCHITECTURE.md) — the current module/runtime architecture,
- [docs/REDUCED_SCOPE_DATA_MODEL.md](docs/REDUCED_SCOPE_DATA_MODEL.md) — the current target tables,
- [docs/SCHEMA.md](docs/SCHEMA.md) — schema currently implemented,
- [SPECIFICATION.md](SPECIFICATION.md) / [docs/PRODUCT_DECISIONS_COMPLETE.md](docs/PRODUCT_DECISIONS_COMPLETE.md) / [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) / [docs/DATA_MODEL.md](docs/DATA_MODEL.md) / [docs/ROADMAP.md](docs/ROADMAP.md) — the original full-scope plan, kept only as long-term reference,
- [docs/HANDOFF_NOTES.md](docs/HANDOFF_NOTES.md) — continuation context for the next coding agent.

## Architecture today

```text
Browser (semantic HTML + vanilla JS enhancement)
        |
        v
Crow HTTP/JSON routes  --(session cookie / CSRF)-->  TicketHub::Application::AuthService
        |                                                       |
        v                                                       v
TicketHub::Application::TicketService  <--(Principal)--  IDatabase (users, sessions, local_credentials)
        |
        v
TicketHub::Infrastructure::Database::IDatabase
        |                         |
        v                         v
PostgresDatabase (libpq)    SqliteDatabase (sqlite3)
```

The reduced-scope V1 target (`docs/REDUCED_SCOPE_ARCHITECTURE.md`) keeps exactly one database port with
two implementations and drops the speculative multi-backend ports (search/mail/jobs/events/cache/
secrets/observability) that the original full-scope architecture reserved.

## Requirements

- CMake 3.25+
- C++20 compiler
- SQLite development files when `TICKETHUB_WITH_SQLITE=ON`
- PostgreSQL client development files when `TICKETHUB_WITH_POSTGRES=ON`
- libargon2 development files (local password hashing)
- Crow 1.3.3 for the server target

Typical Debian dependencies:

```bash
sudo apt install build-essential cmake libpq-dev libsqlite3-dev libargon2-dev libasio-dev
```

## Build and test the core without downloading Crow

```bash
cmake -S . -B build-core \
  -DTICKETHUB_BUILD_SERVER=OFF \
  -DTICKETHUB_WITH_POSTGRES=ON \
  -DTICKETHUB_WITH_SQLITE=ON
cmake --build build-core --parallel 4
ctest --test-dir build-core --output-on-failure
```

## Build the server

```bash
cmake -S . -B build \
  -DTICKETHUB_BUILD_SERVER=ON \
  -DTICKETHUB_WITH_POSTGRES=ON \
  -DTICKETHUB_WITH_SQLITE=ON
cmake --build build --parallel 4
```

CMake first searches for an installed `Crow::Crow`; otherwise it fetches the pinned Crow tag. `vcpkg.json` can provide Crow, libpq and SQLite.

## Administration CLI

The CLI builds without Crow and uses the same database adapters:

```bash
./build-core/ticket-hub-cli version
./build-core/ticket-hub-cli diagnostics
TICKETHUB_DB_DRIVER=sqlite ./build-core/ticket-hub-cli migrate
TICKETHUB_DB_DRIVER=sqlite ./build-core/ticket-hub-cli seed-demo
TICKETHUB_DB_DRIVER=sqlite ./build-core/ticket-hub-cli create-user "person@example.com" "A Person" "a sufficiently long password" [--admin]
```

`diagnostics` redacts the PostgreSQL connection string. Installed deployments should set `TICKETHUB_MIGRATIONS_ROOT` to the installed migration directory when it differs from the compiled development default.

`create-user` is administrator-only account creation: there is no public registration and no invitation
flow in V1 (`REDUCED_SCOPE_SPECIFICATION.md` section 3). The password is set directly by whoever runs
the command; there is no forced-change-on-first-login flow.

## Run with SQLite

```bash
TICKETHUB_DB_DRIVER=sqlite \
TICKETHUB_SQLITE_PATH=./ticket-hub.db \
./build/ticket-hub
```

Open `http://127.0.0.1:8080`.

SQLite has the same planned user-facing feature set, but only one Ticket Hub server process and limited worker concurrency.

## Run with PostgreSQL

```bash
docker compose up -d postgres

TICKETHUB_DB_DRIVER=postgres \
TICKETHUB_DATABASE_URL='host=127.0.0.1 port=5432 dbname=tickethub user=tickethub password=tickethub-dev' \
./build/ticket-hub
```

Do not put production secrets in shell history. The target product uses a pluggable secrets backend.

## Configuration currently implemented

| Variable | Default | Meaning |
|---|---|---|
| `TICKETHUB_DB_DRIVER` | `postgres` | `postgres`, `postgresql`, or `sqlite` |
| `TICKETHUB_DATABASE_URL` | local `tickethub` DB | libpq connection string |
| `TICKETHUB_SQLITE_PATH` | `./ticket-hub.db` | SQLite file |
| `TICKETHUB_BIND_ADDRESS` | `127.0.0.1` | HTTP bind address |
| `TICKETHUB_PORT` | `8080` | HTTP port |
| `TICKETHUB_AUTO_MIGRATE` | `true` | discover/apply schema migrations |
| `TICKETHUB_SEED_DEMO` | `true` | apply idempotent demo data |
| `TICKETHUB_WEB_ROOT` | source `web/` | static web root |
| `TICKETHUB_MIGRATIONS_ROOT` | source `migrations/` | backend migration root |

## Prototype API

The current unversioned demo API will evolve into `/api/v1` in Phase 6 of `docs/REDUCED_SCOPE_ROADMAP.md`,
alongside PAT authentication, fixed rate limits, and numbered pagination.

| Method | Route | Auth required | Purpose |
|---|---|---|---|
| `GET` | `/api/health` | no | health, version and backend |
| `POST` | `/api/auth/login` | no | `{email,password}` → sets session + CSRF cookies |
| `POST` | `/api/auth/logout` | no | clears session (safe to call unauthenticated) |
| `GET` | `/api/auth/me` | session | current principal, or 401 |
| `GET` | `/api/dashboard` | session, or anon if enabled | counts and recent issues |
| `GET` | `/api/projects` | session, or anon if enabled | active project summaries |
| `POST` | `/api/projects` | session + CSRF, global admin | create project |
| `PATCH` | `/api/projects/{key}/archived` | session + CSRF, project admin | `{archived}` |
| `DELETE` | `/api/projects/{key}` | session + CSRF, project admin | move to recycle bin |
| `GET` | `/api/projects/deleted` | session, global admin | list recycle bin |
| `POST` | `/api/projects/{key}/restore` | session + CSRF, global admin | restore from recycle bin |
| `DELETE` | `/api/projects/{key}/permanent` | session + CSRF, global admin | permanently delete |
| `GET` | `/api/settings/anonymous-read` | session | current toggle value |
| `PUT` | `/api/settings/anonymous-read` | session + CSRF, global admin | `{enabled}` |
| `GET` | `/api/issues` | session, or anon if enabled | filter by `project`, `status`, `q` |
| `POST` | `/api/issues` | session + CSRF, project member | create issue (`assigneeEmail`, `parentIssueKey`) |
| `GET` | `/api/issues/{key}` | session, or anon if enabled | current key or permanent alias |
| `PATCH` | `/api/issues/{key}` | session + CSRF, project member | full-replacement edit (D129); see below |
| `PATCH` | `/api/issues/{key}/status` | session + CSRF, project member | `{statusKey, resolution?, expectedVersion?}` |
| `GET` | `/api/issues/{key}/comments` | session, or anon if enabled | live comments |
| `POST` | `/api/issues/{key}/comments` | session + CSRF, project member | add comment |
| `POST` | `/api/issues/{key}/clone` | session + CSRF, project member | simple field-copy clone (D60) |
| `POST` | `/api/issues/{key}/reorder` | session + CSRF, project member | `{beforeIssueKey?}` — manual ordering (D31) |
| `POST` | `/api/issues/{key}/move` | session + CSRF, member of both projects | `{targetProjectKey}` — move to another project (D37) |
| `GET` | `/api/issues/{key}/links` | session, or anon if enabled | links from both ends |
| `POST` | `/api/issues/{key}/links` | session + CSRF, member of both projects | `{targetIssueKey, linkType}` |
| `DELETE` | `/api/issue-links/{id}` | session + CSRF, member of both projects | remove a link |
| `GET` | `/api/issues/{key}/watchers` | session, or anon if enabled | current watchers |
| `POST`/`DELETE` | `/api/issues/{key}/watch` | session + CSRF | watch/unwatch (no project role required) |
| `GET` | `/api/issues/{key}/voters` | session, or anon if enabled | current voters |
| `POST`/`DELETE` | `/api/issues/{key}/vote` | session + CSRF | vote/unvote (no project role required) |
| `DELETE` | `/api/issues/{key}` | session + CSRF, project admin | move issue to recycle bin |
| `GET` | `/api/issues/deleted` | session, global admin | list issue recycle bin |
| `POST` | `/api/issues/{key}/restore` | session + CSRF, global admin | restore issue from recycle bin |
| `DELETE` | `/api/issues/{key}/permanent` | session + CSRF, global admin | permanently delete issue |
| `POST` | `/api/issues/bulk/status` | session + CSRF, project member per issue | `{issueKeys[], statusKey, resolution?}` |
| `POST` | `/api/issues/bulk/assign` | session + CSRF, project member per issue | `{issueKeys[], assigneeEmail?}` |
| `POST` | `/api/issues/bulk/label` | session + CSRF, project member per issue | `{issueKeys[], label}` |
| `POST` | `/api/issues/bulk/delete` | session + CSRF, project admin per issue | `{issueKeys[]}` |

Every `POST /api/issues/bulk/*` route returns `{succeeded: [...], failed: [...]}` — issue keys, not a
single status code — since each key is authorized and processed independently and a partial failure
(unknown key, insufficient role for that particular issue, a workflow-rule violation) does not roll back
the keys that already succeeded.

`PATCH /api/issues/{key}` is a full-replacement edit, not a JSON-merge-patch: `{summary, description?,
priorityKey, assigneeEmail?, storyPoints?, dueDate?, labels?, expectedVersion?}`. Every editable field
is always the caller's intended final value (e.g. omitting `assigneeEmail` unassigns the issue, it does
not leave the current assignee alone) -- the caller is expected to pre-populate the request from the
current issue. It does not change `issueTypeKey` or `parentIssueKey`; neither is editable yet.

Issue responses include `version` and `resolution`. A stale `expectedVersion` returns HTTP 409. A
missing/insufficient project role or global-admin requirement returns HTTP 403. An anonymous read while
the toggle is off returns HTTP 401. A request that violates the fixed workflow's hardcoded rules --
completing an issue without a `resolution`, an unrecognized `resolution`, or completing an issue that
still has an unfinished sub-task -- returns HTTP 422. `parentIssueKey` on issue creation is validated
against the fixed Epic/Sub-task hierarchy (a Sub-task requires a same-project Story/Task/Bug parent, an
Epic may not have one, a Story/Task/Bug's optional parent must be a same-project Epic); a violation
returns HTTP 400, same as any other invalid request field.

`POST /api/issues/{key}/reorder` moves the issue to immediately before `beforeIssueKey` (which must be in
the same project), or to the end of the project if `beforeIssueKey` is omitted/null; the response is the
reordered issue, with the whole project's `rankOrder` values renumbered in one pass. `POST
/api/issues/{key}/move` moves the issue to `targetProjectKey`, allocating a new key/number there; the
vacated key becomes a permanent alias (`GET /api/issues/{oldKey}` keeps resolving to it). Both return
HTTP 400 for an unknown/cross-project anchor, an unknown target project, moving to the issue's current
project, or moving an issue that has a parent or any children.

`linkType` on `POST /api/issues/{key}/links` must be one of the fixed catalog (`blocks`, `relates_to`,
`duplicates`, `clones`); there is no admin-configurable link-type list. Both the source and target
issue's projects must be accessible to the actor (project-Member-or-above), not just the source's.

The watch/vote routes are the one exception among issue writes: they require only an authenticated
session, not project-Member-or-above, since watching/voting is self-referential and doesn't mutate the
issue itself. `POST` is idempotent (watching/voting twice is a no-op, still `200`); `DELETE` on a watch/
vote that doesn't exist is also `200`, not `404`.

Session-authenticated writes require the `X-CSRF-Token` header to match the readable `th_csrf` cookie
set at login (double-submit pattern) — see `src/web/Api.cpp`. **This file has now been compiled and
smoke-tested against a live server** (see "Server verification" below).

The demo UI now has a login screen (`web/index.html`/`app.js`): on load it silently probes
`GET /api/auth/me`; if that returns 401 it shows a sign-in form instead of the app shell. A successful
`POST /api/auth/login` reveals the app shell and shows the signed-in user's name/email/initials in the
sidebar footer, alongside a sign-out button (`POST /api/auth/logout`) that returns to the login screen.
Every non-`GET` request the UI makes now reads the `th_csrf` cookie and attaches it as `X-CSRF-Token`
automatically, and any `401` response from any API call redirects back to the login screen (handles the
session expiring mid-use). Browser-verified end-to-end with Playwright/Chromium against
`127.0.0.1` — see "Server verification" below; Chromium (and other major browsers) treat `localhost`/
`127.0.0.1` as a "potentially trustworthy origin", so the session/CSRF cookies' `Secure` attribute does
not block local HTTP testing, while still requiring real TLS for any other hostname in production.

## Server verification

For most of this project's history, the `ticket-hub` server target (Crow-based) could not be built in the
authoring sandbox because outbound access to `github.com` — needed to fetch Crow via CMake
`FetchContent` — was blocked by the sandbox's network egress policy (the same limitation recorded for the
original prototype in `handoff/IMPLEMENTATION_STATE.md`). **That is no longer the case**: network access
to `github.com` became reachable, and the server target has now been built and smoke-tested end-to-end
against a live HTTP server. Crow 1.3.3 additionally needs standalone `asio` (`sudo apt-get install
libasio-dev`, already listed under "Requirements" above) — install it if `find_package(asio)` fails
during configure.

`src/main.cpp`, `src/web/Api.cpp`, and `src/web/HttpServer.cpp` compiled with **zero warnings or errors
from Ticket Hub's own code** (Crow's own headers emit a large number of `-Wconversion` warnings under
`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`; that's third-party noise, not addressable here). Every
route in the "Prototype API" table above — spanning all three completed phases — was then exercised live
with `curl` against a running instance: login/logout/session validation, CSRF enforcement (missing token
→ 403), project-role enforcement (non-member → 403), the fixed workflow's resolution-required/cleared and
409-conflict rules, full-replacement edit, cloning, issue links, watching/voting, the issue recycle bin,
all four bulk actions, comments, the full project lifecycle, the anonymous-read-access toggle, and the two
newest routes — `POST /api/issues/{key}/reorder` and `POST /api/issues/{key}/move` — including confirming
a moved issue's vacated key still resolves via `issue_key_aliases` through the real HTTP/JSON layer.
**Zero bugs were found** in `Api.cpp` across this sweep — every route, written blind against established
patterns over many prior batches, behaved exactly as documented on the first real test. Full detail is in
`docs/VERIFICATION.md`'s "Server target verified end-to-end" entry.

A follow-up batch then added the login screen described above and verified it with a real, automated
browser (Playwright/Chromium, headless) rather than `curl`: fresh page load shows the login screen and
hides the app shell; signing in with valid credentials shows the app shell, the correct user's name, and
lets every view (dashboard/board/issues/projects) render; creating an issue and changing its status both
succeed (confirming the browser's own `fetch` calls carry the CSRF header correctly, not just `curl` with
a manually-added header); signing out clears both cookies and returns to the login screen, and a page
reload afterward stays on the login screen rather than silently re-entering the app; a wrong password
shows an inline error without ever revealing the app shell. Full detail is in `docs/VERIFICATION.md`.

A follow-up batch then added an Epic/parent picker to the create-issue modal and an inline resolution
picker to the drawer's status control — previously completing an issue via the UI always failed with 422
since `resolution` was never sent, and there was no way to set `parentIssueKey` at all. Both were
browser-verified the same way: creating an Epic then a Story with that Epic as parent (drawer links to
it correctly); a parentless Sub-task showing the server's exact validation message inline; completing an
issue showing/applying the resolution picker; reopening clearing the resolution and hiding the picker for
that direction. A race condition in the picker's own async refresh (caught by this same browser test) was
fixed with a request-id guard. Full detail is in `docs/VERIFICATION.md`.

A third follow-up batch then added full edit, clone, links, and watch/vote to the issue drawer — an
actions row (Watch/Vote toggles with live counts, Clone, Edit) and a Links section (list with correct
bidirectional labels, add form, delete). Browser-verified the same way: watch/vote toggling and
reverting correctly; cloning navigating to the new issue, whose Links section already shows the automatic
`clones` link (D60); adding and deleting a link, confirmed bidirectional (visible and deletable from
either linked issue); a full edit saving correctly and a cancelled edit discarding its changes. This
testing caught a genuine CSS layout bug — a link row could overflow into the drawer's sidebar column and
block clicks on whatever sat underneath it there (`.link-list`, a CSS grid container, was letting its
items claim their full content width instead of shrinking) — fixed with an explicit `min-width: 0`.

The issue drawer now covers the full single-issue lifecycle. A fourth follow-up batch then added
project-management UI: a "New project" modal, per-card Archive/Unarchive and Delete buttons, and a
recycle-bin view (visible and usable only for global administrators, matching D88). Browser-verified the
same way, including a non-admin seeing neither the recycle-bin toggle nor a working create-project action
(inline 403). This testing also caught a real state-management bug — `state` (current view, selected
project, filters) was never reset on logout, so a second user in the same browser tab could land on
whatever the first user last had open, including a project they can't access or one just
archived/deleted — fixed by resetting all of `state` on every login-screen transition, not just the
signed-in user.

A fifth follow-up batch then added the issue recycle bin, symmetric to the project one: a Delete button in
the drawer's actions row, and a recycle-bin toggle in the Issues view (global-admin-only, with
Restore/Delete-permanently per row). Browser-verified the same way, including a project-role-insufficient
delete attempt failing with the server's exact 403 message rather than silently succeeding.

Still missing from `web/`: bulk actions and reorder/move UI. Those routes are all live-verified via `curl`
(above) but still not reachable from the demo UI.

What **was** compiled and tested in this environment, with all warnings enabled
(`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`), for both SQLite and PostgreSQL build configurations:

- `ticket-hub-core` (domain, application, infrastructure/database — including the identity/session code,
  fixed project-role authorization, project lifecycle, the anonymous-read-access toggle, the fixed
  hierarchy/workflow rules, full-replacement issue edit, the fixed issue-link catalog, simple cloning,
  self-service watching/voting, the issue recycle bin, simple bulk actions, manual ordering with
  renumbering, and moving an issue between projects, in both database adapters),
- `ticket-hub-cli` (including `create-user`),
- all seven test binaries (`ctest --output-on-failure`): `domain_validation_tests`, `migration_tests`,
  `sqlite_integration_tests` (`editIssue`: every field, label replacement, assignee clearing, the
  stale-version conflict, one `issue_history` row per changed field; issue links: create, list from both
  ends, duplicate/self-link rejection, find-by-id, delete; watch/vote: idempotency, listing,
  unknown-issue rejection; issue recycle bin: soft-delete/restore/list/permanent-delete lifecycle,
  idempotent no-ops, comment cascade on permanent delete; manual ordering: renumbering on
  reorder-before-anchor and reorder-to-end, cross-project and self-anchor rejection; move: target-project
  rank/counter allocation, alias creation and resolution, `issue_history` write, and rejection of
  same-project moves, unknown-project moves, and moving an issue with a parent or with children),
  `identity_integration_tests` (create-user, login success/failure, generic-error anti-enumeration check,
  minimal lockout, session validate/expire/logout), `authorization_integration_tests` (project-role
  gating on issue writes/edits/cloning/links/reorder/move — including the "member of source but not
  target project" move case, not-found semantics under authorization, the anonymous-read-access toggle,
  the full project lifecycle: create/archive/soft-delete/restore/permanently-delete against both
  project-admin and global-administrator paths, confirming watch/vote require no project role unlike
  everything else, the issue recycle bin's project-admin-vs-global-admin split, and bulk actions applying
  the same per-issue authorization on a mixed batch of accessible/inaccessible/unknown keys),
  `workflow_integration_tests` (every Epic/Sub-task hierarchy rejection case, resolution
  required/rejected-if-unknown on completion, resolution cleared on reopen, the sub-task-completion gate,
  reopening leaving a sub-task's status untouched, clone field-copy correctness, the
  sub-task-parent-retention special case, and basic link lifecycle), and `crypto_tests` — all passing.
- Additionally, migrations, seed data, `create-user`, and a full login → validate-session → logout cycle
  were manually verified end-to-end against a **live local PostgreSQL 16 server** (not just SQLite) in
  Phase 1; Phase 2 repeated this for the PostgreSQL adapter's authorization/project-lifecycle code
  (`createProject`, `setProjectArchived`, `softDeleteProject`, `listDeletedProjects`, `restoreProject`,
  `permanentlyDeleteProject`, `installation_settings` get/set); Phase 3 repeated it six times, for
  `createIssue`/`changeIssueStatus` (hierarchy, resolution, sub-task gate), `editIssue` (every field,
  label replacement, assignee clearing, stale-version conflict), issue links/cloning (create/list/
  duplicate-and-self-link rejection/find/delete, plus `cloneIssue` including the sub-task special case),
  watch/vote (idempotency, listing, unwatch/unvote, unknown-issue rejection), the issue recycle bin plus
  all four bulk actions (through `TicketService`), and `reorderIssue`/`moveIssue` (renumbering, target
  rank/counter allocation, alias resolution, and the same-project/unknown-project/parent/children
  rejection cases, through `PostgresDatabase` directly) — all passing.

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) are now
built and live-verified as described in "Server verification" above — including every route added across
Phases 1-3: the session-cookie/CSRF wiring, the project-CRUD and anonymous-read-toggle routes, the
`parentIssueKey`/`resolution` request fields, the `PATCH /api/issues/{key}` full-edit route, the HTTP 422
mapping for `Domain::WorkflowViolation`, `POST /api/issues/{key}/clone`,
`GET`/`POST /api/issues/{key}/links`, `POST`/`DELETE /api/issues/{key}/watch`,
`GET /api/issues/{key}/watchers`, `POST`/`DELETE /api/issues/{key}/vote`,
`GET /api/issues/{key}/voters`, `DELETE /api/issue-links/{id}`, `DELETE /api/issues/{key}`,
`GET /api/issues/deleted`, `POST /api/issues/{key}/restore`, `DELETE /api/issues/{key}/permanent`,
`POST /api/issues/bulk/{status,assign,label,delete}`, `POST /api/issues/{key}/reorder`, and
`POST /api/issues/{key}/move`. (Earlier, while adding the edit route, three existing routes --
`POST /api/issues`, `PATCH /api/issues/{key}/status`, `POST /api/issues/{key}/comments` -- were found by
inspection to be missing a `catch (const Domain::Forbidden&)` handler, which would have surfaced a
project-role authorization failure as HTTP 500 instead of 403; fixed before this verification pass, and
the live sweep confirms the fix actually works end-to-end.)

Schema migrations are files such as `001_initial.sql` and `003_product_foundation.sql`. The runner:

1. discovers and sorts schema files,
2. excludes `_seed_` files,
3. calculates a stable content checksum,
4. verifies already-applied checksums,
5. applies each new migration transactionally,
6. records the version and checksum.

After a migration is released, edit it only by adding a new migration. A changed applied migration is rejected.
