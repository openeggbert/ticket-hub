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
- **a demo web UI covering every Phase 1-3 write route** (login, hierarchy/resolution pickers, full issue
  edit/clone/links/watch-vote/delete, project management, both recycle bins, reorder/move/bulk actions —
  browser-verified with Playwright/Chromium, see "Server verification" below) plus a simple Kanban board,
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
- **comment editing and tombstone delete** (D81/D82/D83, Phase 4, partial): an `edited_at` timestamp
  instead of a version-history table; soft-delete via the same columns issues/projects already use, no
  separate admin recycle-bin API for comments; simplified permissions — the comment's own author can
  always edit/delete it, otherwise the actor needs project-Admin-or-above (or global admin),
- every issue/comment/project write now takes an explicit `Principal` instead of a fixed demo user,
- PostgreSQL and SQLite adapters,
- ordered schema migration discovery with stored checksums,
- PostgreSQL migration advisory lock,
- issue optimistic-lock versioning for status updates,
- permanent project key-alias schema foundation, and a permanent issue key-alias mechanism actually
  written to by `moveIssue` (D38): the vacated key stays permanently resolvable to the moved issue,
- recycle-bin schema foundations and live-query filtering,
- domain, migration, crypto, SQLite integration, identity, authorization, and workflow tests (see "Known
  verification limitation" below for what is *not* yet compiled/tested in this environment),
- **Phase 4 (Collaboration) and Phase 5 (Attachments and Kanban board) are both fully complete**, closing
  out Milestone 2: fixed emoji reactions, @mention handles and in-app notifications, the Markdown editor
  (toolbar/live preview/full upload+drag-drop+paste attachment support), simplified worklogs, the
  admin/security audit log, ad-hoc issue filter/search widening, the personal dashboard, Kanban board WIP
  limits, and the full attachments vertical (local filesystem storage, four native-element previews,
  sortable list, recycle bin) — see the batch-by-batch history below and `docs/VERIFICATION.md` for exactly
  what was built and verified in each.

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
TICKETHUB_DB_DRIVER=sqlite ./build-core/ticket-hub-cli create-user "person@example.com" "A Person" "a sufficiently long password" [--admin] [--handle=<handle>]
```

`diagnostics` redacts the PostgreSQL connection string. Installed deployments should set `TICKETHUB_MIGRATIONS_ROOT` to the installed migration directory when it differs from the compiled development default.

`create-user` is administrator-only account creation: there is no public registration and no invitation
flow in V1 (`REDUCED_SCOPE_SPECIFICATION.md` section 3). The password is set directly by whoever runs
the command; there is no forced-change-on-first-login flow. `--handle` sets the optional, unique @mention
handle (D56/D80) -- there is no self-service profile-editing flow yet to set or change it afterward.

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
| `TICKETHUB_ATTACHMENTS_DIR` | source `data/attachments/` | local filesystem attachment storage root (D15) -- point this at a persistent, backed-up volume in a real deployment |

## Prototype API

The current unversioned demo API will evolve into `/api/v1` in Phase 6 of `docs/REDUCED_SCOPE_ROADMAP.md`,
alongside PAT authentication, fixed rate limits, and numbered pagination.

| Method | Route | Auth required | Purpose |
|---|---|---|---|
| `GET` | `/api/health` | no | health, version and backend |
| `POST` | `/api/auth/login` | no | `{email,password}` → sets session + CSRF cookies |
| `POST` | `/api/auth/logout` | no | clears session (safe to call unauthenticated) |
| `GET` | `/api/auth/me` | session | current principal, or 401 |
| `GET` | `/api/dashboard` | session, or anon if enabled | counts, recent issues, and (authenticated only, D24) assigned-to-me/watched/upcoming-deadline issues |
| `GET` | `/api/board-columns` | session, or anon if enabled | one entry per fixed workflow status with its optional soft WIP limit (D32/D33) |
| `PUT` | `/api/board-columns/{statusKey}` | session + CSRF, global admin | `{wipLimit}` (number or null); installation-wide, not per-project |
| `GET` | `/api/users` | session | user directory (id/displayName/email/handle) for @mention autocomplete (D80) |
| `GET` | `/api/notifications` | session | `?unread=true` filters; fixed set (D14) |
| `GET` | `/api/notifications/unread-count` | session | `{count}` |
| `POST` | `/api/notifications/{id}/read` | session + CSRF | scoped to the caller's own notifications |
| `POST` | `/api/notifications/read-all` | session + CSRF | scoped to the caller's own notifications |
| `GET` | `/api/admin/audit-events` | session, global admin | newest 200 admin/security events (D23) |
| `GET` | `/api/projects` | session, or anon if enabled | active project summaries |
| `POST` | `/api/projects` | session + CSRF, global admin | create project |
| `PATCH` | `/api/projects/{key}/archived` | session + CSRF, project admin | `{archived}` |
| `DELETE` | `/api/projects/{key}` | session + CSRF, project admin | move to recycle bin |
| `GET` | `/api/projects/deleted` | session, global admin | list recycle bin |
| `POST` | `/api/projects/{key}/restore` | session + CSRF, global admin | restore from recycle bin |
| `DELETE` | `/api/projects/{key}/permanent` | session + CSRF, global admin | permanently delete |
| `GET` | `/api/settings/anonymous-read` | session | current toggle value |
| `PUT` | `/api/settings/anonymous-read` | session + CSRF, global admin | `{enabled}` |
| `GET` | `/api/issues` | session, or anon if enabled | filter by `project`, `status`, `type`, `priority`, `assignee`, `label`, `dueBefore`, `q` (ad-hoc only, D10/D43; `q` also matches description) |
| `POST` | `/api/issues` | session + CSRF, project member | create issue (`assigneeEmail`, `parentIssueKey`) |
| `GET` | `/api/issues/{key}` | session, or anon if enabled | current key or permanent alias |
| `PATCH` | `/api/issues/{key}` | session + CSRF, project member | full-replacement edit (D129); see below |
| `PATCH` | `/api/issues/{key}/status` | session + CSRF, project member | `{statusKey, resolution?, expectedVersion?}` |
| `GET` | `/api/issues/{key}/comments` | session, or anon if enabled | live comments |
| `POST` | `/api/issues/{key}/comments` | session + CSRF, project member | add comment |
| `PATCH` | `/api/issues/{key}/comments/{id}` | session + CSRF, author or project admin | `{body, expectedVersion?}` — full-replacement edit (D81) |
| `DELETE` | `/api/issues/{key}/comments/{id}` | session + CSRF, author or project admin | tombstone delete (D82) |
| `GET` | `/api/issues/{key}/comments/{id}/reactions` | session, or anon if enabled | current reactions |
| `POST`/`DELETE` | `/api/issues/{key}/comments/{id}/reactions/{key}` | session + CSRF | react/un-react (no project role required, D84) |
| `GET` | `/api/issues/{key}/worklogs` | session, or anon if enabled | logged time entries |
| `POST` | `/api/issues/{key}/worklogs` | session + CSRF, project member | `{workDate, timeSpentSeconds, comment?}` (D12/D13) |
| `PATCH` | `/api/issues/{key}/worklogs/{id}` | session + CSRF, project member | full-replacement edit, no own-vs-others split |
| `DELETE` | `/api/issues/{key}/worklogs/{id}` | session + CSRF, project member | tombstone delete, no own-vs-others split |
| `GET` | `/api/issues/{key}/attachments` | session, or anon if enabled | active attachments (D15/D98-D105) |
| `POST` | `/api/issues/{key}/attachments` | session + CSRF, project member | `multipart/form-data`, one `file` part; fixed 25MB/20-per-issue limits, D98 |
| `DELETE` | `/api/issues/{key}/attachments/{id}` | session + CSRF, uploader or project admin | tombstone delete (D101) |
| `GET` | `/api/attachments/{id}/download` | session, or anon if enabled | raw bytes with `Content-Type`/`Content-Disposition`; not nested under `/issues/{key}`, since a download/preview URL only ever needs the id |
| `GET` | `/api/attachments/deleted` | session, global admin | recycle bin, 90-day on-demand retention (D102) |
| `POST` | `/api/attachments/{id}/restore` | session + CSRF, global admin | restore from recycle bin |
| `DELETE` | `/api/attachments/{id}/permanent` | session + CSRF, global admin | permanently delete (and its file) |
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

`PATCH /api/issues/{key}/comments/{id}` is a full-replacement edit of `body` (D81), sharing the same
`expectedVersion`/409 optimistic-locking contract as issue edits, and sets an `editedAt` timestamp on the
response -- there is no stored history of the comment's prior text, just the fact that it was edited.
`DELETE /api/issues/{key}/comments/{id}` is a tombstone delete (D82): the row and original body stay in
the database, simply excluded from `GET /api/issues/{key}/comments` afterward -- there is no separate
recycle-bin API for comments, unlike issues and projects. Permissions on both are simplified (D83): the
comment's own author may always edit/delete it; otherwise the actor needs project-Admin-or-above (or
global admin) — not the edit-own/edit-all/delete-own/delete-all matrix the original spec described.

The watch/vote routes are the one exception among issue writes: they require only an authenticated
session, not project-Member-or-above, since watching/voting is self-referential and doesn't mutate the
issue itself. `POST` is idempotent (watching/voting twice is a no-op, still `200`); `DELETE` on a watch/
vote that doesn't exist is also `200`, not `404`.

`{key}` in `POST`/`DELETE /api/issues/{key}/comments/{id}/reactions/{key}` is a path segment from the
fixed eight-reaction catalog (D84: `thumbs_up`, `thumbs_down`, `laugh`, `hooray`, `confused`, `heart`,
`rocket`, `eyes` -- GitHub's well-known reaction set, chosen as a conservative default since the decision
register calls for "a fixed reaction set" without enumerating one). Reactions are self-service like
watch/vote (no project-role check), and each user may add each reaction key at most once per comment;
`POST`/`DELETE` are idempotent the same way watch/vote are. `GET .../reactions` returns
`{items: [{reactionKey, user}, ...]}` -- the caller groups by `reactionKey` for counts/highlighting, the
same "server stays dumb, client aggregates" split used for issue links.

`GET /api/users` is a directory listing (id/displayName/email/handle only -- no isAdmin/active/timeZone),
requiring a session even when the installation-wide anonymous-read toggle is on, since the user directory
is more sensitive than issue data. It backs @mention autocomplete (D80) and is the only way the demo UI
discovers handles.

The fixed in-app notification set (D14) is created as a side effect of three existing writes, never
directly by an API caller: `POST`/`PATCH /api/issues` notifies a newly-set or changed assignee (skipping
self-assignment and a no-op re-save with the same assignee); `POST /api/issues/{key}/comments` notifies
every `@handle` mention resolved in the body (D80) and every watcher of the issue except the comment's
own author, with mentioned taking priority over watched for a recipient who is both (one notification,
not two). `GET /api/notifications` and `GET /api/notifications/unread-count` are always scoped to the
caller's own notifications; `POST /api/notifications/{id}/read` returns `{ok: false}` rather than 404 for
an unknown id or someone else's notification (there is no cross-user notification management, so there is
nothing more specific to report). Mentions are parsed only when a comment is created, not on every edit,
to avoid re-notifying on every save of an already-mentioning comment.

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

A sixth and final follow-up batch added manual reordering (an Order column with move-up/move-down buttons
on the Issues table, shown only with a single project selected, since the reorder anchor must be in the
same project), moving an issue to another project (a picker in the drawer), and simple bulk actions
(checkboxes plus a bulk-action bar for status/assign/label/delete). Browser-verified the same way,
including confirming the new checkboxes and reorder buttons don't also open the issue drawer despite
living inside the same clickable table row (`event.stopPropagation()`, caught and fixed proactively during
implementation rather than by a failing test).

With this, `web/` covers every write route added across Phases 1-3 -- there is no remaining gap between
what the API exposes and what the demo UI can reach.

A seventh batch started Phase 4 (Collaboration): comment editing and tombstone delete (D81/D82/D83), the
first Phase 4 feature. Added `IDatabase::editComment`/`deleteComment`/`findCommentById` in both adapters
(migration `008_comment_editing.sql` adds `comments.edited_at`), matching `TicketService` methods with
simplified author-or-project-admin permissions, the two new API routes above, and Edit/Delete controls in
the drawer's comment list (shown only for the comment's author or a global admin -- a client-side
simplification, not the actual security boundary, since the client never loads per-project role
information the way it would need to for a project-admin-but-not-author case). Browser-verified: adding,
editing (with an "(edited)" marker appearing), cancelling an in-progress edit (discards the change), and
deleting a comment all work through the real HTTP layer; a non-author, non-global-admin actor sees no
Edit/Delete buttons on someone else's comment at all (the client-side simplification above), and the
authorization tests separately confirm the server itself also rejects such an attempt with 403.

An eighth batch added the next Phase 4 feature: fixed emoji reactions on comments (D84). Added
`comment_reactions` (migration `009_comment_reactions.sql`, a three-column composite-key many-to-many
table mirroring `issue_watchers`/`issue_votes`) with `IDatabase::addCommentReaction`/
`removeCommentReaction`/`listCommentReactions` in both adapters, matching `TicketService` methods with
the same self-service/no-project-role reasoning as watch/vote, and the two new API routes above. Since
the decision register calls for "a fixed reaction set" without naming one, this uses GitHub's own
well-known eight-reaction set (`thumbs_up`, `thumbs_down`, `laugh`, `hooray`, `confused`, `heart`,
`rocket`, `eyes`) as a conservative, familiar default -- documented explicitly as a filled product-decision
gap. `web/` renders all eight as small pill buttons under each comment, showing a per-reaction count and
highlighting the ones the current viewer has added; clicking toggles react/un-react through the real HTTP
layer. Browser-verified: reacting shows the button go active with a "1" count; clicking again removes it
(button reverts, count disappears); a second, different reaction key can coexist with an active one; and,
switching to a second user, the count is shared (visible to both) while each user's own "active" highlight
is independent -- confirmed by alex reacting to a comment demo had already reacted to and the count going
from 1 to 2 without alex seeing demo's own active state carried over.

A ninth batch added @mention handles and the fixed in-app notification set (D56/D80/D14). Migration
`010_mentions_and_notifications.sql` adds `users.handle` (optional, unique via a partial index) and
`notifications` (`user_id`, `type`, `issue_id` nullable, `read_at` -- the exact minimal shape in
`docs/REDUCED_SCOPE_DATA_MODEL.md`). `ticket-hub-cli create-user` gained `--handle=<handle>`; the three
seeded demo accounts now have handles (`demo`/`alex`/`sam`). `TicketService::createIssue`/`editIssue`
notify a newly-set or changed assignee; `addComment` parses `@handle` tokens out of the body (once, at
creation) and notifies each resolved user, plus every watcher of the issue except the comment's own
author -- a recipient who is both mentioned and watching gets exactly one notification, the more specific
reason winning. New `GET /api/users` (directory listing for autocomplete) and the four
`/api/notifications*` routes above. `web/` gained a notification bell with an unread-count badge in the
top bar (clicking a notification marks it read and opens the related issue, with a "mark all read"
button) and an @mention autocomplete dropdown under the comment textarea (both add and edit), backed by
the cached `/api/users` directory. Browser-verified end-to-end: creating an issue assigned to a second
user shows exactly one unread notification for them; typing `@sa` in the comment box shows a matching
autocomplete suggestion that inserts the full handle on click; posting a comment that mentions a user
notifies them with the correct issue reference; opening the notification panel, clicking an item, and
using "mark all read" all update the badge correctly through the real HTTP layer; and a full regression
re-run of the eighth batch's reaction test and the seventh batch's comment-editing test both still pass
unchanged.

A tenth batch closed out D16 (rich text): comment bodies and issue descriptions now render as formatted
Markdown instead of plain escaped text, with a visual toolbar and a live preview toggle on every
Markdown-capable textarea (comment add, comment edit, issue description on create and edit) -- entirely
in `web/`, no schema or API change, since bodies are still stored and transmitted as raw Markdown text.
`renderMarkdown`/`renderMarkdownInline` implement a deliberately small subset (bold, italic, inline code,
links, headings, lists, blockquotes, fenced code, horizontal rules), safe by construction: the raw text
is HTML-escaped *first* (the same `escapeHtml` used everywhere else in `web/`), and every transform after
that only ever wraps the already-escaped text in a fixed, hardcoded set of tags -- user input can never
introduce a real HTML tag or attribute this way, so there is no separate sanitization pass that could be
wrong. Link targets are restricted to `http(s)`/`mailto`; any other scheme (`javascript:`, etc.) is left
as literal `[text](url)` text rather than becoming a clickable link. Browser-verified, including two
security-focused checks: a comment body containing `<script>...</script>` and an `onerror`-bearing `<img>`
tag renders as inert, visible literal text (confirmed via a page-level flag that the payload never
executes) rather than as markup, and a `[label](javascript:alert(1))` link renders as literal bracket-
paren text rather than a clickable anchor. Caught and fixed one real rendering bug during implementation,
before it reached a security concern: the first cut of the italic regex used `_..._` as an alternative to
`*...*`, which mishandled text containing two separate double-underscore identifiers (e.g.
`__init__`-style names) by treating an underscore from the *first* pair and one from the *second* pair as
matching open/close delimiters, silently swallowing everything in between into a single (still safely
escaped, just visually wrong) `<em>` span -- fixed by dropping underscore-delimited emphasis entirely and
supporting only `**bold**`/`*italic*`, which have no such adjacency ambiguity. Also verified no regression
in the eighth batch's reaction test, the seventh batch's comment-editing test, and the ninth batch's
mentions/notifications test (unaffected by the `.comment-body-text` markup changing from `<p>` to `<div>`
to legally contain the new block-level Markdown output).

An eleventh batch added simplified worklogs (D12/D13). Migration `011_worklogs.sql` adds `worklogs`
(`id`, `issue_id`, `author_user_id`, `work_date`, `time_spent_seconds`, `comment` nullable, plus the same
tombstone-delete and `version` columns comments/issues already use) -- no remaining-estimate linkage,
since D12 dropped time estimates from V1 entirely, so there is nothing for a worklog to adjust. The
permission model deliberately differs from comments: D13 drops the own-vs-others edit/delete split
entirely, so `TicketService::addWorklog`/`editWorklog`/`deleteWorklog` all require only
project-Member-or-above on the issue's project -- any project member may edit or delete *any* worklog on
an issue they can access, not just the one they logged themselves (unlike D83's author-or-admin rule for
comments). `editWorklog` shares the same `expectedVersion` -> `Domain::ConcurrencyConflict` (409)
optimistic-locking contract as comment/issue edits. New `GET`/`POST /api/issues/{key}/worklogs` and
`PATCH`/`DELETE /api/issues/{key}/worklogs/{id}` routes. `web/` gained a "Time tracking" section in the
issue drawer: a list of logged entries (duration formatted as e.g. "1h 30m", author, date, optional
comment) each with a Delete button shown unconditionally (no client-side author check, since the server
itself allows any project member to delete any entry), and a log-time form accepting a free-text duration
like "1h 30m" or "45m" (parsed client-side, with an HTML5 `pattern` attribute as a first line of defense
and a JS-level parse-and-toast fallback). Browser-verified: logging time shows the correct formatted
duration and comment in the list; deleting an entry removes it; an unparseable duration is rejected before
it reaches the server. Verified against live PostgreSQL directly (`addWorklog`/`listWorklogs`/
`findWorklogById`/`editWorklog`, including the stale-version-conflict rejection, and `deleteWorklog`).

A twelfth batch added the simple append-only admin/security audit log (D23) -- the last item in Phase 4
(Collaboration), which is now fully implemented per `docs/REDUCED_SCOPE_ROADMAP.md`. Migration
`012_audit_log.sql` adds `audit_events` (`id`, `category`, `action`, `actor_user_id` nullable,
`target_type`/`target_id` nullable, `details` nullable, `created_at`) -- no categories-as-a-retention-
feature, export, or configurable retention beyond what's here; rows are simply appended and never updated
or purged. Rather than hooking every write in the codebase, a small, deliberately focused set of
admin/security-relevant actions record an event as a side effect: `AuthService::login` on a wrong
password (`auth`/`login.failed`) or an attempt against an already-locked account (`auth`/`login.blocked`),
`AuthService::createUser` (`identity`/`user.created`, with no actor since `ticket-hub-cli create-user`
runs outside any web session), and `TicketService::setAnonymousReadEnabled`/`permanentlyDeleteProject`/
`permanentlyDeleteIssue` (all `admin`-category). `IDatabase::recordAuditEvent` is fire-and-forget (`void`,
unlike `createNotification`, whose result the notification list feature reads back immediately);
`listAuditEvents(limit)` is newest-first with no pagination or filtering. New
`GET /api/admin/audit-events` route and `TicketService::listAuditEvents`, both global-administrator-only,
the same access level as the recycle bins. `web/` gained a new "Audit log" nav item (hidden for
non-admins, shown and hidden again on logout to avoid leaking it to whoever logs in next in the same
browser tab) rendering a simple read-only table. Browser-verified: the nav item is invisible to a
non-admin and visible to the global admin; toggling the anonymous-read setting on and off produces two
rows in the log showing the correct action and actor. Verified against live PostgreSQL directly,
including confirming that a CLI-driven `create-user` call actually produced an `identity`/`user.created`
event with no actor, end-to-end through the real CLI binary (not just a direct database call).

A thirteenth batch started Phase 5 (Attachments and Kanban board) with ad-hoc issue filter/search
widening (D10/D43). `Domain::IssueFilter` gained `issueTypeKey`/`priorityKey`/`assigneeEmail`/`label`/
`dueBefore`, alongside the pre-existing `projectKey`/`statusKey`/`search` -- still the ad-hoc, in-UI-only
filter model (no saved/shared filters, no JQL, not usable as a webhook/board source). `SqliteDatabase::
listIssues`/`PostgresDatabase::listIssues` both widened to match: type/priority/assignee are equality
joins against already-present query aliases; `dueBefore` is an inclusive `<=`; `label` is a fresh `EXISTS`
subquery against `issue_labels`/`labels` rather than a condition on the already-joined/aggregated
label-list column used to display an issue's labels, so a label filter narrows matches without truncating
a matching issue's own label list; `search` now also matches the issue description, not just summary/
issue key, per D43's plain-substring, no-full-text-index scope. `GET /api/issues` accepts matching new
query parameters; `TicketService::listIssues` needed no change. `web/`'s Issues view filter bar gained
type/priority/assignee dropdowns, a label input, and a due-date picker; the "Clear" button and the
Board-view/global-search transitions all reset the new fields too, so a lingering ad-hoc filter can't leak
into a different view. New SQLite-integration test coverage for every new field individually, a combined
multi-field filter, the inclusive `dueBefore` boundary, and an explicit check that filtering by label
doesn't corrupt the filtered issue's own label list. Verified against live PostgreSQL directly (a
standalone smoke-test program exercising the same cases against `PostgresDatabase::listIssues`) and
browser-verified with Playwright/Chromium (each filter narrows the Issues table correctly, "Clear"
restores the full list, and the Board view doesn't inherit a lingering Issues-view filter), plus a full
regression re-run of the markdown/mentions/reactions/worklog/audit-log/comment-editing browser tests.

A fourteenth batch continued Phase 5 with personal dashboard widgets (D24). `Domain::DashboardStats`
gained `assignedToMe`/`watchedIssues`/`upcomingDeadlines`, matching D24's fixed widget set (assigned
issues, watched issues, recent activity, deadlines, simple stats -- no active-sprint widget, since Scrum
was removed for V1). New `IDatabase::listWatchedIssues(userId, limit)` in both adapters, the reverse
direction of the existing `listWatchers`. `TicketService::dashboard` personalizes for an authenticated
actor -- `assignedToMe` reuses the existing `listIssues` assignee filter and excludes Done-category
issues, `upcomingDeadlines` is derived from that same result set app-side rather than a second database
round trip, `watchedIssues` calls the new method -- and all three stay empty for an anonymous viewer.
`GET /api/dashboard` gained the three new arrays. `web/`'s Dashboard view gained "Assigned to me", "Issues
I'm watching", and "Upcoming deadlines" panels, shown only when a principal is present. New
SQLite-integration coverage for `listWatchedIssues` and authorization-integration coverage for the
dashboard personalization (anonymous gets empty widgets even with anonymous read enabled; Done-category
issues excluded from assigned-to-me; watched-issues reflects a fresh watch). Verified against live
PostgreSQL directly (`listWatchedIssues` across multiple users, overlapping watches, the `limit`
parameter, and cleanup back to empty). Browser-verified with Playwright/Chromium: a real Watch-button
click in the issue drawer populates the watching widget after returning to the dashboard; a real due-date
edit through the API populates the deadlines widget with the correct formatted date; two different users
(alex, sam) each see their own personalized widgets, not each other's. A first draft of the browser script
produced confusing results from state left over by an earlier run that crashed mid-test (a stale watch on
an issue from a script that hit a drawer-backdrop click interception); re-running against a freshly
reseeded server produced clean results, confirming the confusion was test-script state pollution across
runs against the same long-lived dev server, not an application bug.

A fifteenth batch continued Phase 5 with Kanban board WIP limits (D32/D33). New migration
`013_board_columns.sql` adds `board_columns` -- a single flat, installation-wide table (`id`, `status_id`
unique FK to `issue_statuses`, `wip_limit` nullable, `sort_order`) with no `board_id`/`project_id` column
at all, following `docs/REDUCED_SCOPE_DATA_MODEL.md`'s target schema literally and D32's "one board
column equals one workflow status": a WIP limit set on a column applies to that status's column on every
project's board, since there is no per-project board identity in the reduced-scope model.
`002_seed_demo.sql` seeds the five rows ("In Progress" given a demo limit of 3, the rest unlimited). New
`Domain::BoardColumn`; `IDatabase::listBoardColumns()` (ordered by `sort_order`) and
`setBoardColumnWipLimit(statusKey, optional<int>)` (`false` for an unknown status key) in both adapters;
matching `TicketService` methods (read is the same access rule as projects/issues, set is
global-administrator-only like the anonymous-read toggle, an unknown status key throws
`std::invalid_argument`). New `GET /api/board-columns` and `PUT /api/board-columns/{statusKey}` routes.
`web/`'s Board view shows each column's live count as `N / limit` (or plain `N` when unlimited) with a
soft, display-time-only highlight when over limit -- never blocking a status change or issue creation into
that column -- and gives global admins an inline editor to set/clear each column's limit. Drag-and-drop
board reordering was deliberately left out of this batch: neither D32 nor D33 mentions it, and the
roadmap's "board usable end-to-end" exit gate was already satisfied by the pre-existing click-to-drawer
status change. New SQLite-integration and authorization-integration test coverage. Verified against live
PostgreSQL directly (five seeded columns, the demo WIP limit, set/clear/unknown-key cases). Browser-verified
with Playwright/Chromium: a non-admin sees counts only, a global admin sees and can use the inline editor,
pushing a column over its limit shows the highlight and raising the limit clears it, and switching to a
second project while the setting is unchanged confirms it is genuinely installation-wide rather than
scoped to whichever project's board happened to be open when it was changed.

A sixteenth batch completed Phase 5 (and closed out Milestone 2) with the full attachments vertical
(D15/D98-D105). `attachments.sha256`/`deleted_at`/`deleted_by_user_id` already existed from
`003_product_foundation.sql`, pre-provisioned well ahead of this phase; migration `014_attachments.sql`
adds only the `issue_id` index that table never got. New `Domain::Attachment` (`id`, `issueId`,
`issueKey` -- resolved via a join purely for the recycle bin's display, `uploader`, `fileName`,
`contentType`, `byteSize`, `sha256`, `createdAt`, `deletedAt`). `IDatabase::createAttachment` is the one
create* method that takes a caller-supplied `id`: the local filesystem storage key (D15, hardwired, no
storage-backend abstraction) must be known -- and the file already written -- before the row is inserted,
so a row never describes a file that doesn't exist on disk. `listAttachments`/`findAttachmentById`/
`softDeleteAttachment`/`restoreAttachment`/`listDeletedAttachments`/`permanentlyDeleteAttachment` in both
adapters mirror the issue/project/comment tombstone pattern; `listAttachmentStorageKeysForIssue`/
`...ForProject` return every attachment's storage key regardless of soft-delete state, used to delete
files on disk before a permanent issue/project delete cascades through the database (D105 has no
periodic orphan-file audit at all). New `src/infrastructure/storage/LocalAttachmentStorage`: a plain,
non-virtual class (D15 -- no abstract storage port, so no S3-shaped extension point either), keyed by the
attachment's own UUID, rooted at `TICKETHUB_ATTACHMENTS_DIR`. New `Domain::validateAttachmentUpload`
enforces D98's fixed limits (25MB/file, 20/issue, a blocked-extension denylist -- no admin configuration,
no MIME allow-list, no quotas). `TicketService::uploadAttachment` (project-Member-or-above) computes the
SHA-256 at upload time (D105, never re-verified); `downloadAttachment`/`listAttachments` mirror comments'
read-access rule; `deleteAttachment` is uploader-or-project-Admin-or-above (mirroring D83's comment rule,
the closest precedent); `listDeletedAttachments` implements D102's fixed 90-day on-demand retention
itself, one layer above the SQL adapter, since purging an attachment also means deleting its file.
`permanentlyDeleteIssue`/`permanentlyDeleteProject` were both extended to collect and delete affected
attachment files before the database cascade runs. New `GET`/`POST /api/issues/{key}/attachments`,
`DELETE /api/issues/{key}/attachments/{id}`, `GET /api/attachments/{id}/download` (not nested under
`/issues/{key}`, since a download/preview URL only ever needs the id), and the recycle-bin routes (all
global-admin-only). `web/`'s issue drawer gained a sortable Attachments section (D101: name/size/date/
uploader/type), drag-and-drop upload, and native-element previews for all four D99 kinds (`<img>` for
images, `<iframe>` for PDF and text, `<audio>`/`<video>` for the rest). The Markdown toolbar gained full
upload + drag/drop + paste (D100) wherever an issue key is already known (both comment textareas, the
issue-edit description -- deliberately not the create-issue form, since no issue exists yet to attach to),
inserting `![name](attachment://id)`/`[name](attachment://id)` at the cursor;
`renderMarkdownInline` gained real image-syntax support (previously absent entirely) and resolves
`attachment://<id>` to a real download URL, validating the id shape first and leaving anything malformed
as inert text -- the same "safe by construction" posture as the existing `javascript:`-scheme guard. A new
admin-only "Attachment recycle bin" nav item mirrors the audit log's visibility pattern. New
SQLite-integration and authorization-integration test coverage (full CRUD, both permission rules, the
fixed limits, the recycle-bin split). Verified against live PostgreSQL directly. Extensively
browser-verified with Playwright/Chromium: upload/preview/sort/delete through the real UI; all four
preview kinds rendering the correct native element; real native `drop`/`paste` DOM events (not just
`setInputFiles`) working on both the dedicated dropzone and directly on the Markdown editor; an inserted
`attachment://` reference actually resolving to a working download link once a comment is posted and
rendered; and the full recycle-bin restore/permanent-delete flow. Two real bugs were caught and fixed
before this could be considered complete: a Postgres-only "inconsistent types deduced for $1" error from
reusing one placeholder for two differently-typed columns in the `INSERT`, and a redundant first-draft
migration that tried to re-add three columns the schema already had (caught immediately by `ctest`,
never shipped). **This closes out Phase 5 -- every item in `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 5 list
is now implemented, and Milestone 2 is fully closed.**

What **was** compiled and tested in this environment, with all warnings enabled
(`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`), for both SQLite and PostgreSQL build configurations:

- `ticket-hub-core` (domain, application, infrastructure/database — including the identity/session code,
  fixed project-role authorization, project lifecycle, the anonymous-read-access toggle, the fixed
  hierarchy/workflow rules, full-replacement issue edit, the fixed issue-link catalog, simple cloning,
  self-service watching/voting, the issue recycle bin, simple bulk actions, manual ordering with
  renumbering, moving an issue between projects, and (Phase 4, now complete) comment editing/tombstone
  delete, fixed emoji reactions, @mention handles/the fixed in-app notification set, simplified worklogs,
  the admin/security audit log, and (Phase 5, now complete) the widened ad-hoc issue filter/search model,
  personal dashboard widgets, Kanban board WIP limits, and the full attachments vertical, in both database
  adapters), plus `Infrastructure::Storage::LocalAttachmentStorage`,
- `ticket-hub-cli` (including `create-user`),
- all seven test binaries (`ctest --output-on-failure`): `domain_validation_tests`, `migration_tests`,
  `sqlite_integration_tests` (`editIssue`: every field, label replacement, assignee clearing, the
  stale-version conflict, one `issue_history` row per changed field; issue links: create, list from both
  ends, duplicate/self-link rejection, find-by-id, delete; watch/vote: idempotency, listing,
  unknown-issue rejection; issue recycle bin: soft-delete/restore/list/permanent-delete lifecycle,
  idempotent no-ops, comment cascade on permanent delete; manual ordering: renumbering on
  reorder-before-anchor and reorder-to-end, cross-project and self-anchor rejection; move: target-project
  rank/counter allocation, alias creation and resolution, `issue_history` write, and rejection of
  same-project moves, unknown-project moves, and moving an issue with a parent or with children; comment
  editing: version increment, `editedAt` set, stale-edit conflict, editing an unknown comment; tombstone
  delete: soft-deleted comments excluded from listing and `findCommentById`, the row and body still
  physically present, deleting an already-deleted comment is a no-op; comment reactions: idempotent
  add/remove per (comment, user, key), multiple users and multiple keys per comment listed correctly;
  `findUserByHandle` resolving the seeded handle and returning nullopt for an unknown one; notifications:
  `createNotification` resolving the issue key via the stored `issue_id`, `listNotifications`/
  `countUnreadNotifications` with the `unreadOnly` filter, `markNotificationRead`/
  `markAllNotificationsRead` idempotency and per-user scoping),
  `identity_integration_tests` (create-user, login success/failure, generic-error anti-enumeration check,
  minimal lockout, session validate/expire/logout, handle normalization/uniqueness/format validation for
  create-user's optional `--handle`), `authorization_integration_tests` (project-role
  gating on issue writes/edits/cloning/links/reorder/move — including the "member of source but not
  target project" move case, not-found semantics under authorization, the anonymous-read-access toggle,
  the full project lifecycle: create/archive/soft-delete/restore/permanently-delete against both
  project-admin and global-administrator paths, confirming watch/vote require no project role unlike
  everything else, the issue recycle bin's project-admin-vs-global-admin split, bulk actions applying
  the same per-issue authorization on a mixed batch of accessible/inaccessible/unknown keys, comment
  edit/delete's simplified author-or-project-admin permissions, including the global-admin-can-moderate-
  any-comment case and unknown-comment nullopt/false returns, comment reactions requiring no project
  role (like watch/vote) while still rejecting an unknown reaction key or an unknown comment id, and the
  fixed notification set -- assigned/self-assigned/unchanged-reassign, mentioned/unknown-handle/
  self-mention, watched-comment/self-watch-self-comment, the mentioned-wins-over-watched dedupe, and
  per-user scoping of mark-read/mark-all-read, each isolated in its own issue with an explicit
  markAllNotificationsRead reset between sub-tests so no sub-test's leftover watcher state can
  contaminate the next one's assertions),
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
  rejection cases, through `PostgresDatabase` directly); Phase 4 repeated it for `editComment`/
  `deleteComment`/`findCommentById` (version increment, `editedAt`, stale-edit conflict, tombstone
  exclusion from listing, no-op on an already-deleted comment), for `addCommentReaction`/
  `removeCommentReaction`/`listCommentReactions` (idempotent add/remove, multiple users and reaction keys
  per comment listed correctly), and for `findUserByHandle` plus `createNotification`/
  `listNotifications`/`countUnreadNotifications`/`markNotificationRead`/`markAllNotificationsRead`
  (issue-key resolution via the stored `issue_id`, the `unreadOnly` filter, idempotent mark-read/
  mark-all-read, per-user scoping), through `PostgresDatabase` directly — all passing. `ticket-hub-cli
  create-user --handle=<handle>` was also exercised directly against a live PostgreSQL server.

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
