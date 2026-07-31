# Verification record

## 2026-07-31 — Full edit, clone, links, and watch/vote UI added to the issue drawer

Added the next slice of `web/` UI identified as missing: everything the issue drawer could reach through
the API but had no controls for -- full edit, clone, links (list/add/delete), and watch/vote.

### What changed

- `web/app.js`: `openIssue()` now fetches links/watchers/voters alongside the issue/comments (5 parallel
  requests instead of 2), and its rendering was restructured into an inner `render(editing)` closure so
  the same fetched data can re-render in read or edit mode without a refetch. Added `editFieldsMarkup()`
  (the edit-mode field set), a drawer actions row (Watch/Vote toggle buttons with live counts, Clone,
  Edit), a Links section (list with bidirectional outward/inward labels, add form, delete buttons), and a
  Parent meta row (clickable, when set).
- `web/styles.css`: `.drawer-actions`, `.link-list`/`.link-row`/`.link-form` styles.

### Two real bugs caught by this batch's own browser testing

1. **Layout bug**: `.link-list` is a CSS grid container; grid items get an implicit content-based minimum
   width unless overridden, so `.link-row` refused to shrink below its own intrinsic content width and
   visually overflowed into the drawer's meta sidebar column -- Playwright's click-action safety check
   ("element intercepts pointer events") caught this directly, refusing to click a delete-link button that
   was actually covered by the sidebar. Confirmed via `getBoundingClientRect()`: before the fix the link
   row's right edge sat at x=1090.8 while the sidebar's left edge was at x=1036 (an ~82px real overlap
   region); after adding `min-width: 0` to `.link-row`, the row's right edge moved to x=1008, clear of the
   sidebar. This was a genuine bug a real user would have hit too, not a test artifact.
2. **Test-script bug, not an app bug, but worth recording**: an earlier version of the browser test used
   an ambiguous `.link-row` selector and picked up the automatic `clones` link (created by the earlier
   Clone action, per D60) instead of the manually-added `relates_to` link, making it look like the "add
   link" feature wasn't working. Rewritten to scope the assertion to the row matching the intended link
   label, which confirmed the feature was correct all along.

### Verification

Standalone Playwright/Chromium scripts (same approach as prior UI batches), against a locally running
server, SQLite, demo-seeded:

1. Watch toggle: "☆ Watch (0)" → click → "★ Watching (1)" → click again → back to "☆ Watch (0)". Vote
   toggle: same pattern independently.
2. Clone: clicking Clone on TH-1 navigates the drawer to the new cloned issue (a fresh key), whose Links
   section already shows the automatic `clones` link back to TH-1 (D60) without any extra action.
3. Links: starting from 1 link row (the automatic clone link), adding a `relates_to` link to TH-2 brings
   the count to 2; the new row shows TH-2's actual summary and is clickable, navigating the drawer to
   TH-2; from TH-2's own drawer the same link appears (bidirectional) and its delete button works,
   bringing TH-2's link count back to 0.
4. Full edit: opening Edit on TH-1 shows editable fields pre-filled with current values; changing
   summary/description/priority/assignee/story points/labels and clicking Save persists correctly (the
   drawer re-renders in read mode showing the new values, `edit-error` stays hidden); opening Edit again,
   changing the summary, and clicking Cancel discards the change (the drawer shows the previously-saved
   summary, not the discarded one).
5. Re-ran the login and hierarchy/resolution-picker browser tests from the two prior batches against the
   same build to confirm no regression -- both still pass unchanged.

All checks passed after the layout fix; no committed test files or screenshots (scratchpad only).

### What is still not built

Project-management UI (create/archive/recycle-bin), the issue recycle bin, bulk actions, and reorder/move
still have no UI in `web/` -- see `NEXT.md`.

## 2026-07-31 — Resolution picker and Epic/parent picker added to the demo UI

Fixed two broken UI paths identified while planning the next batch of `web/` work: completing an issue
via the drawer's status `<select>` always 422'd (no `resolution` was ever sent), and the create-issue
modal had no field for `parentIssueKey` at all (and was even missing "Sub-task" from the issue-type
list), so a Sub-task could never be created from the UI and a Story/Task/Bug could never be linked to an
Epic from the UI.

### What changed

- `web/app.js`: `RESOLUTIONS` catalog and `resolutionLabel()`; `issueTypeHierarchyLevel()` (mirrors
  `Domain::issueTypeHierarchyLevel` for client-side picker narrowing only -- the server remains the actual
  source of truth); `applyStatusChange()` helper; `refreshCreateParentOptions()` which fetches the
  selected project's issues and filters them by hierarchy level to populate the Epic/parent picker.
- `web/index.html`: added `sub-task` to the create-issue-type `<select>`; added the Epic/parent `<select>`
  (`#create-parent`), hidden/shown and relabeled based on the selected issue type.
- `web/app.js` (drawer): the status `<select>`'s change handler now checks the target status's category;
  for a Done-category target it reveals an inline resolution row (select + Confirm/Cancel) instead of
  calling the API immediately; Confirm calls `applyStatusChange` with the chosen resolution. The drawer
  also now displays the issue's resolution (once set, read-only) and its parent issue key (once linked,
  clickable -- scoped with a dedicated `#drawer-parent-link` id and a single listener, deliberately
  *not* reusing the page-wide `bindIssueLinks()` helper, which would have re-registered duplicate click
  listeners on every background view element still mounted behind the drawer).
- `web/styles.css`: small additions for the resolution `<select>` and its Confirm/Cancel button row.

### A real bug caught during this batch's own browser testing

The project (`#create-project`) and issue-type (`#create-issue-type`) `<select>` elements both trigger
`refreshCreateParentOptions()` on `change`. A Playwright test that selected project then issue type in
quick succession (`page.selectOption` twice with no wait between) produced a duplicated Epic entry in the
picker -- two in-flight async calls interleaved: the first call's fetch resolved and appended options
*after* the second call had already reset and re-populated the list. Fixed with a monotonically
increasing request-id guard (`createParentRequestId`) that makes a call's `select.innerHTML +=` a no-op
once a newer call has started, so only the most recent selection's results ever get applied. The fix was
verified by re-running the same test and confirming exactly one option per matching issue.

### Verification

Standalone Playwright/Chromium scripts (same approach as the login-screen batch) against a locally running
server, SQLite, demo-seeded:

1. Selecting "Epic" as the issue type hides the Epic/parent picker entirely (an Epic cannot have a
   parent, D64); created an Epic issue successfully.
2. Selecting "Story" shows "Epic (optional)" and the picker's options include the just-created Epic
   (exactly once, confirming the race-condition fix); created a Story with that Epic as parent; the
   drawer showed a "Parent" row linking to the Epic; clicking it navigated to the Epic's own drawer.
3. Selecting "Sub-task" shows "Parent (required)"; submitting without picking one produced the server's
   exact validation message (`A sub-task must have a parent issue`) inline in the create-error banner,
   confirming the client correctly defers to server-side validation rather than duplicating it.
4. Created a fresh Task issue, then selected "Done" in the drawer's status picker: the resolution row was
   hidden before the selection and visible after; picking "Fixed" and clicking Confirm applied the status
   change with the resolution, which then displayed read-only in the drawer's Resolution row.
5. Reopening that same issue (selecting a non-Done status) cleared the resolution (server-side, D68-D70)
   and hid the resolution row again for that direction, with no picker shown (resolution is only required
   moving *into* a Done-category status, never out of one).

All checks passed after the request-id fix; no committed test files or screenshots (scratchpad only, per
the established pattern from the login-screen batch).

### What is still not built

Full edit, links, clone, watch/vote, the issue recycle bin, bulk actions, reorder/move, and
project-management UI still don't exist in `web/` -- see `NEXT.md`.

## 2026-07-31 — Minimal login screen added to the demo UI, verified with a real headless browser

Added a login screen to `web/index.html`/`app.js`/`styles.css` (the "Immediate next step" identified in
the previous entry below): the demo UI previously had no authentication flow at all, so `TicketService`'s
project-role enforcement, verified live via `curl` in the previous entry, was still unreachable from the
UI itself, and the CSRF-token attachment the UI's own `fetch` calls needed was entirely missing.

### What changed

- `web/index.html`: a `#login-screen` (email/password form) sibling to the existing `#app-shell`, both
  toggled via a shared `.hidden` class; the sidebar footer now shows the signed-in user's initials/name/
  email (previously a hardcoded "Demo User") plus a sign-out button.
- `web/app.js`: `api()` now reads the `th_csrf` cookie and attaches it as `X-CSRF-Token` on every
  non-`GET` request (previously never sent from the UI at all -- every write from the demo UI would have
  403'd the moment CSRF enforcement went live); any `401` response from any API call (except the two auth
  probes themselves) shows the login screen, handling a session expiring mid-use; `init()` now silently
  probes `GET /api/auth/me` before doing anything else and shows the login screen instead of loading data
  if that fails; the login form calls `POST /api/auth/login`, then re-probes `/api/auth/me` to populate
  the sidebar footer; the sign-out button calls `POST /api/auth/logout` and returns to the login screen.
- `web/styles.css`: a centered login card matching the existing design system (same input/button/
  form-error styles as the create-issue modal).

### Verification

Standalone Node.js scripts driving Playwright against the pre-installed Chromium
(`executablePath: '/opt/pw-browsers/chromium'`, `playwright-core` installed ad hoc into the scratchpad
directory, not committed to the repository) against a locally running `ticket-hub` server
(SQLite, auto-migrated, demo-seeded):

1. Fresh page load: `#login-screen` visible, `#app-shell` hidden, zero cookies present.
2. Submitting valid credentials (`demo@ticket-hub.local` / `demo12345`): both `th_session` (`Secure`,
   confirmed via `page.context().cookies()`) and `th_csrf` cookies get set: `#app-shell` becomes visible,
   `#login-screen` hides, and the sidebar footer shows "Demo User" -- confirming `Secure` cookies are
   **not** a blocker for local testing, since Chromium (and other major browsers) treat `localhost`/
   `127.0.0.1` as a "potentially trustworthy origin" exempt from the Secure-requires-HTTPS restriction;
   this does not weaken the production security posture (a real deployment hostname still requires TLS
   for the cookie to be set at all).
3. Every existing view renders after login: dashboard (stat cards populated), issues list (table rows),
   board (Kanban columns), projects (project cards) -- all previously untestable end-to-end because
   nothing could authenticate.
4. Created a new issue via the create-issue modal (`POST /api/issues`, a CSRF-protected write) through the
   browser's own `fetch` -- succeeded, opened the issue drawer, no error shown. This is the first time a
   CSRF-protected write from the *browser UI itself* (not `curl` with a manually-added header) was
   confirmed to work, proving `app.js`'s new automatic CSRF-header attachment functions correctly.
5. Changed the new issue's status via the drawer's status `<select>` (`PATCH .../status`, also
   CSRF-protected) -- succeeded, confirmation toast shown.
6. Clicked sign-out: both cookies cleared, `#login-screen` shown again. Reloaded the page afterward: still
   on the login screen (not silently re-entering the app), confirming the server actually invalidated the
   session rather than the UI merely hiding it client-side.
7. Submitted a wrong password: inline error message shown ("Invalid email or password"), `#app-shell`
   never becomes visible.

All seven checks passed on the first attempt; no code changes were needed after the initial
implementation. Screenshots were captured for visual sanity-check (login screen and post-login dashboard)
and discarded after review (not committed -- generated artifacts, not source).

### What is still not built

Only the login screen exists. There is still no UI for project management (create/archive/recycle-bin),
issue hierarchy/resolution pickers, full edit, links, clone, watch/vote, the issue recycle bin, bulk
actions, or the reorder/move actions added earlier this session -- all of those remain reachable only via
direct API calls (all live-verified via `curl`, see the entry below), not through the demo UI. See
`NEXT.md` for the itemized list.

## 2026-07-31 — Server target verified end-to-end for the first time (Crow build succeeded)

Every prior entry in this log recorded the same standing limitation: outbound access to `github.com` was
blocked, so the Crow-based `ticket-hub` server target had never been compiled, and every Phase 1-3 route
in `src/web/Api.cpp` was written blind against established patterns. That changed this session: network
access to `github.com` is now reachable from the sandbox, so the server target was built and exercised
against a live HTTP server for the first time.

### What changed in the environment

- `git ls-remote https://github.com/CrowCpp/Crow.git` succeeds; CMake's `FetchContent_Declare`/
  `FetchContent_MakeAvailable(Crow)` fetches Crow 1.3.3 successfully.
- Crow 1.3.3's `CMakeLists.txt` additionally requires standalone `asio` (not Boost.Asio), which was not
  preinstalled. Installed via `sudo apt-get install -y libasio-dev` (already listed as a dependency in
  `README.md`'s "Requirements"/apt install line, so no doc change was needed there) — this pulled in
  `libboost-dev` and related packages as transitive dependencies of the Ubuntu package, not because
  Ticket Hub itself needs Boost.

### Build

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel "$(nproc)"
```

Configured and built clean on the first attempt after `libasio-dev` was installed. `-Wall -Wextra
-Wpedantic -Wconversion -Wshadow` surfaces a large number of warnings **from Crow's own headers**
(`mustache.h`, `routing.h`, `http_server.h`, `http_connection.h`, `parser.h` — mostly `-Wconversion` on
`size_t`/`uint64_t` narrowing in Crow's implementation) — expected third-party noise, not addressable in
this repository. Isolating just `src/main.cpp`, `src/web/Api.cpp`, and `src/web/HttpServer.cpp` (the only
files this repository owns that only compile when `TICKETHUB_BUILD_SERVER=ON`) confirms **zero warnings
and zero errors from Ticket Hub's own code** — every route added across Phases 1-3, blind against
established patterns for months of prior batches, compiled correctly the first time it was actually
type-checked. `ctest --output-on-failure` is still 7/7 with the server target also built.

### Live HTTP smoke test

Launched the built `ticket-hub` binary against a fresh SQLite database (`TICKETHUB_AUTO_MIGRATE=true
TICKETHUB_SEED_DEMO=true`, bound to `127.0.0.1:18080`) and drove it with `curl`, covering essentially
every route in the "Prototype API" table in `README.md`:

- `GET /api/health` — 200, correct backend/version.
- `POST /api/auth/login` (demo, alex) — 200, `Set-Cookie` for both `th_session` (HttpOnly) and `th_csrf`
  (readable) as designed; `GET /api/auth/me` reflects the session; `POST /api/auth/logout` then
  `GET /api/auth/me` — 401, confirming the session is actually gone, not just cookie-cleared client-side.
- CSRF enforcement: `POST /api/issues` without `X-CSRF-Token` — 403; with a valid token — 201.
- Project-role enforcement: Sam (TH member, not a WEB member) creating a WEB issue — 403; Alex (not a
  global admin) creating a project — 403; the global admin — 201.
- Fixed workflow rules: completing a fresh issue without `resolution` — 422; with a valid `resolution` —
  200, `resolution` and incremented `version` present in the response; a stale `expectedVersion` — 409.
  (An earlier pass in this same sweep initially targeted `TH-1`, which the demo seed already has in
  `done` status — that call correctly returned 200/no-op rather than 422, which is the documented
  "resolution is ignored when the status does not actually change" behavior, not a bug; re-ran against a
  freshly created issue to actually exercise the required-resolution path.)
- **`POST /api/issues/{key}/reorder`** (D31, this session's most recent batch): reordering an issue
  before another renumbers both correctly (`rankOrder` reflects the new order on `GET`).
- **`POST /api/issues/{key}/move`** (D37, this session's most recent batch): moving `TH-7` into `WEB`
  returns the issue with `projectKey: "WEB"`, `key: "WEB-3"`; `GET /api/issues/TH-7` (the vacated key)
  still resolves to the same issue via the `issue_key_aliases` alias written by `moveIssue` — confirming
  the alias mechanism works through the real HTTP/JSON layer, not just the database layer directly.
- Full-replacement issue edit (`PATCH /api/issues/{key}`), cloning (`POST .../clone`), issue links
  (`POST`/`GET .../links`, `DELETE /api/issue-links/{id}`), watching/voting and their unwatch/unvote/list
  counterparts, the issue recycle bin (`DELETE`/`POST .../restore`/`DELETE .../permanent`,
  `GET /api/issues/deleted`), all four bulk actions (`POST /api/issues/bulk/{status,assign,label,delete}`
  — confirmed `succeeded`/`failed` key lists), comments (`POST`/`GET .../comments`), the full project
  lifecycle (`POST /api/projects`, `PATCH .../archived`, `DELETE`/`POST .../restore`/
  `DELETE .../permanent`, `GET /api/projects/deleted`), and the anonymous-read-access toggle
  (`GET`/`PUT /api/settings/anonymous-read`, confirmed `GET /api/projects` goes 401 → 200 as the toggle
  flips, and that only a global admin may flip it) — every one exercised live, every one behaved exactly
  as documented in `README.md`'s "Prototype API" section. **Zero bugs found** in `Api.cpp` across this
  entire sweep.
- The scratch SQLite database, server process, and all temporary cookie-jar files were removed after the
  sweep; nothing from this verification was committed.

### What is still not verified

There is still no login page or any authentication UI in `web/` — the demo UI browses/creates issues
without ever calling `/api/auth/login`, so it will start failing 401s against a real deployment where
session enforcement is live end-to-end, exactly as `README.md` already notes. Building a minimal login
page (and ideally project-management/hierarchy/reorder/move UI) is the next real gap, not a verification
gap — see `NEXT.md`.

## 2026-07-31 — Phase 3 complete at the core/CLI/test layer (manual ordering and moving between projects, reduced scope)

Verified in the same session/environment as the batches below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (restarted first, as in every prior batch this
session; freshly created scratch database/role, dropped afterward).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel "$(nproc)"
ctest --test-dir build --output-on-failure
```

New `IDatabase::reorderIssue`/`moveIssue` methods in both adapters, plus `TicketService::reorderIssue`/
`moveIssue`, compiled cleanly. Also verified the SQLite-only (`-DTICKETHUB_WITH_POSTGRES=OFF`) and
PostgreSQL-only (`-DTICKETHUB_WITH_SQLITE=OFF`) configurations build clean, both with
`-DTICKETHUB_BUILD_SERVER=OFF`.

A migration bug was caught during this batch and fixed before it reached a committed test failure: the
`007_ranking.sql` backfill (`UPDATE issues SET rank_order = issue_number`) only reaches rows that exist at
migration-apply time, but `002_seed_demo.sql` is applied through a separate, non-checksummed code path
(`discoverMigrationFiles` excludes `_seed_` filenames from the ordered migration run) that in practice
runs *after* `migrate()` completes -- so every seeded issue was getting the column's `DEFAULT 0` instead
of a real rank. Fixed by setting `rank_order` explicitly in the seed `INSERT` (both backends) rather than
relying on the migration backfill for seed data.

### Passing tests (7/7 — same binaries, extended coverage)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-identity-tests`,
   `ticket-hub-workflow-tests`, `ticket-hub-crypto-tests` — unchanged, all still passing.
2. `ticket-hub-sqlite-integration-tests` — extended: reordering TH-3 before TH-1 renumbers the whole
   project so TH-3 ranks lower than TH-1; reordering TH-3 with no anchor appends it to the end (ranks
   higher than every other TH issue); reordering relative to an issue in a different project and
   reordering an issue before itself are both rejected; moving TH-2 into WEB allocates `WEB-3`, appends
   its rank after WEB's existing issues, and the vacated `TH-2` key resolves via `issue_key_aliases` back
   to `WEB-3`, with one `issue_history` row (`field_name = 'project'`); moving to the issue's own project,
   to an unknown project, an issue that has children, and an issue that has a parent are all rejected.
3. `ticket-hub-authorization-tests` — extended: a non-member cannot reorder another project's issue; a TH
   member can reorder a TH issue; moving an issue requires project-Member-or-above on **both** the source
   and target projects -- a TH member who is not a WEB member is rejected moving either direction, while a
   member of both (TH member, WEB admin) succeeds.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: ran a standalone program (`pg_rank_move_smoke.cpp`, not committed)
  against `PostgresDatabase` directly, covering the same renumbering, cross-project-anchor-rejection,
  move/alias/history, same-project-rejection, and parent/children-rejection cases as the SQLite test. All
  assertions passed. The scratch database and role were dropped after verification.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all seven test binaries
  compile and link cleanly, including in the SQLite-only and PostgreSQL-only build configurations.

### Environment limitations (unchanged)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com` remains blocked. The new
`POST /api/issues/{key}/reorder` and `POST /api/issues/{key}/move` routes, and the new `rankOrder` field
on the issue JSON representation, follow the same patterns as the already-unverified Phase 1-3 routes and
carry the same caveat.

## 2026-07-31 — Phase 3, partial continued (issue recycle bin and bulk actions, reduced scope)

Verified in the same session/environment as the batches below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (the server needed restarting first, as in every
prior batch this session; freshly created scratch database/role, dropped afterward).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings, including through
the four new `IDatabase` methods (`softDeleteIssue`/`restoreIssue`/`listDeletedIssues`/
`permanentlyDeleteIssue`) forcing a rebuild through both adapters and `TicketService`. Bulk actions added
no new `IDatabase` methods at all -- they compose the existing single-issue operations.

### Passing tests (7/7 — same binaries, extended coverage)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-identity-tests`,
   `ticket-hub-workflow-tests`, `ticket-hub-crypto-tests` — unchanged, all still passing.
2. `ticket-hub-sqlite-integration-tests` — extended: soft-deletes the previously-created issue, asserts
   it disappears from ordinary lookup, asserts soft-deleting it again is a no-op, asserts it appears in
   `listDeletedIssues`, asserts it can be restored and found again, re-deletes it, permanently deletes
   it, asserts a repeat permanent delete returns `false`, and asserts its comments are gone from the
   `comments` table afterward (direct `COUNT(*)` query, confirming the `ON DELETE CASCADE` foreign key
   actually fires, not just that the issue itself can no longer be looked up).
3. `ticket-hub-authorization-tests` — extended: a non-member cannot soft-delete an issue; a project admin
   (not global admin) can; listing/restoring/permanently-deleting are rejected for the project admin and
   permitted for the global administrator, mirroring the project recycle bin's D88 split exactly. Bulk
   actions: `bulkAssign` on a mixed batch of two accessible TH issues, one inaccessible WEB issue, and one
   unknown key reports 2 succeeded / 2 failed; `bulkAddLabel` and `bulkChangeStatus` succeed for an
   accessible batch; `bulkDelete` soft-deletes a batch and the deleted issues are confirmed gone from
   ordinary lookup afterward.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: ran `ticket-hub-cli migrate`/`seed-demo`, then compiled and ran a
  standalone program (`pg_recycle_bulk_smoke.cpp`, not committed) exercising `PostgresDatabase::
  softDeleteIssue`/`restoreIssue`/`listDeletedIssues`/`permanentlyDeleteIssue` directly, then wrapping
  the same `PostgresDatabase` in a `TicketService` to exercise `bulkAssign` (including the unknown-key
  partial-failure case), `bulkAddLabel`, `bulkChangeStatus`, and `bulkDelete`. All 15 assertions passed.
  The scratch database and role were dropped after verification.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all seven test binaries
  compile and link cleanly, including in the SQLite-only and PostgreSQL-only build configurations.

### Environment limitations (unchanged)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com` remains blocked. The new
`DELETE /api/issues/{key}`, `GET /api/issues/deleted`, `POST /api/issues/{key}/restore`,
`DELETE /api/issues/{key}/permanent`, and `POST /api/issues/bulk/{status,assign,label,delete}` routes
follow the same patterns as the already-unverified Phase 1-3 routes and carry the same caveat.

## 2026-07-31 — Phase 3, partial continued (watchers and voting, reduced scope)

Verified in the same session/environment as the batches below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (the server needed restarting first, as in every
prior batch this session; freshly created scratch database/role, dropped afterward).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings, including through
the six new `IDatabase` methods (`watchIssue`/`unwatchIssue`/`listWatchers`/`voteIssue`/`unvoteIssue`/
`listVoters`) forcing a rebuild through both adapters and `TicketService`.

### Passing tests (7/7 — same binaries, extended coverage)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-identity-tests`,
   `ticket-hub-workflow-tests`, `ticket-hub-crypto-tests` — unchanged, all still passing.
2. `ticket-hub-sqlite-integration-tests` — extended: watches the previously-created issue with two
   different users, asserts watching again is a no-op, asserts both watchers are listed, asserts
   unwatching removes one and unwatching again is a no-op; the same sequence for voting; asserts
   watching an unknown issue key raises `std::invalid_argument`.
3. `ticket-hub-authorization-tests` — extended: Sam, who is not a member of the WEB project at all
   (unlike every other actor/action tested in this file), can still watch and vote on a WEB issue,
   confirming watch/vote are deliberately exempt from the project-role check every other issue write
   enforces; watching/voting again is a no-op; both are visible to any authenticated reader.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: ran `ticket-hub-cli migrate`/`seed-demo` (confirmed `issue_watchers`
  and `issue_votes` were created with the expected columns, composite primary key, and `ON DELETE
  CASCADE` foreign keys via `psql \d`), then compiled and ran a standalone program
  (`pg_watch_vote_smoke.cpp`, not committed) exercising `PostgresDatabase::watchIssue`/`unwatchIssue`/
  `listWatchers`/`voteIssue`/`unvoteIssue`/`listVoters` directly: idempotency of watch/vote, two
  independent watchers, listing, unwatch/unvote removing exactly the right row, and voting on an unknown
  issue key raising `std::invalid_argument`. All 13 assertions passed. The scratch database and role
  were dropped after verification.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all seven test binaries
  compile and link cleanly, including in the SQLite-only and PostgreSQL-only build configurations.

### Environment limitations (unchanged)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com` remains blocked. The new
`POST`/`DELETE /api/issues/{key}/watch`, `GET /api/issues/{key}/watchers`,
`POST`/`DELETE /api/issues/{key}/vote`, and `GET /api/issues/{key}/voters` routes follow the same
patterns as the already-unverified Phase 1-3 routes and carry the same caveat.

## 2026-07-31 — Phase 3, partial continued (issue links and cloning, reduced scope)

Verified in the same session/environment as the batches below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (freshly created for this batch and dropped
afterward, same as every prior PostgreSQL verification this session).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings, including through
the four new `IDatabase` methods (`createIssueLink`/`listIssueLinks`/`findIssueLinkById`/
`deleteIssueLink`) forcing a rebuild through both adapters and `TicketService`.

### Passing tests (7/7 — same binaries, extended coverage)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-identity-tests`,
   `ticket-hub-crypto-tests` — unchanged, all still passing.
2. `ticket-hub-sqlite-integration-tests` — extended: creates a link from the previously-created issue to
   a seeded one, asserts it is visible as outward from the source and inward from the target, asserts an
   exact-duplicate link and a self-link are both rejected (the self-link by the database `CHECK`
   constraint), asserts `findIssueLinkById` resolves both ends, and asserts the link can be deleted
   (and that deleting it again returns `false`).
3. `ticket-hub-workflow-tests` — extended: clones an issue and asserts summary/description/type/
   priority/labels are copied but assignee and the parent/Epic link are not; asserts the clone has an
   outward `clones` link to the original and the original has the inverse inward link; clones a Sub-task
   and asserts its original parent is retained (the structural exception); and a basic link
   create/list/delete lifecycle through `TicketService`, including rejecting a self-link and an unknown
   link type.
4. `ticket-hub-authorization-tests` — extended: a non-member cannot clone another project's issue; a
   project admin can; creating a link between a project the actor can access and one they cannot is
   rejected even though the source project is fine; a non-member of either linked project cannot delete
   the link; a member of both can.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: created a scratch `tickethub` database/role, ran `ticket-hub-cli
  migrate`/`seed-demo`, then compiled and ran a standalone program (`pg_links_smoke.cpp`, not committed)
  exercising both the raw `PostgresDatabase` link methods (create/list-from-both-ends/duplicate-and-
  self-link rejection/find-by-id/delete) and, through a `TicketService` wrapping the same
  `PostgresDatabase`, `cloneIssue` (field-copy correctness, the automatic `clones` link, and the
  sub-task-parent-retention special case). All 13 assertions passed. The scratch database and role were
  dropped after verification.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all seven test binaries
  compile and link cleanly, including in the SQLite-only and PostgreSQL-only build configurations.

### Environment limitations (unchanged)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com` remains blocked. The new
`POST /api/issues/{key}/clone`, `GET`/`POST /api/issues/{key}/links`, and
`DELETE /api/issue-links/{id}` routes follow the same patterns as the already-unverified Phase 1-3
routes and carry the same caveat.

## 2026-07-31 — Phase 3, partial continued (full issue edit, reduced scope)

Verified in the same session/environment as the batches below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (freshly created for this batch and dropped
afterward, same as every prior PostgreSQL verification this session).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings through the
`IDatabase::editIssue` addition (interface change forced a rebuild through `TicketService` and both
adapters, same discipline as the `changeIssueStatus` signature change in the prior batch).

### Passing tests (7/7 — same binaries, extended coverage)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-identity-tests`,
   `ticket-hub-workflow-tests`, `ticket-hub-crypto-tests` — unchanged, all still passing.
2. `ticket-hub-sqlite-integration-tests` — extended: edits every standard field on the previously-created
   issue (summary, description, priority, assignee, story points, due date, labels), asserts the
   optimistic-lock version increments and the resolution set by the earlier status-change test is left
   undisturbed, asserts a stale-version edit raises `Domain::ConcurrencyConflict`, asserts the assignee
   can be cleared, and asserts at least six `issue_history` rows were written (one per changed field).
3. `ticket-hub-authorization-tests` — extended: a non-member cannot edit another project's issue
   (`Domain::Forbidden`); a project member can edit a project issue; editing an unknown issue key returns
   `std::nullopt` rather than throwing.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: created a scratch `tickethub` database/role (the server needed
  restarting first, as in the prior batch), ran `ticket-hub-cli migrate`/`seed-demo`, then compiled and
  ran a standalone program (`pg_edit_smoke.cpp`, not committed) linking directly against
  `PostgresDatabase`: created an issue, edited every field (summary, description, priority, assignee,
  story points, due date, labels) and confirmed each was persisted and the version incremented, confirmed
  a stale-version edit raises `ConcurrencyConflict`, confirmed the assignee can be cleared, and confirmed
  editing an unknown issue key returns `std::nullopt`. All 11 assertions passed. The scratch database and
  role were dropped after verification.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all seven test binaries
  compile and link cleanly, including in the SQLite-only and PostgreSQL-only build configurations.

### Environment limitations (unchanged)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com` remains blocked. The new
`PATCH /api/issues/{key}` full-edit route follows the same patterns as the already-unverified Phase 1-3
routes and carries the same caveat. While adding it, three existing routes (`POST /api/issues`,
`PATCH /api/issues/{key}/status`, `POST /api/issues/{key}/comments`) were found missing a
`catch (const Domain::Forbidden&)` handler and fixed -- this was caught by code inspection while editing
the surrounding code, not by compiling, since the file still cannot be built in this sandbox.

## 2026-07-31 — Phase 3, partial (fixed workflow and hierarchy, reduced scope)

Verified in the same session/environment as Phases 1-2 below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (freshly created for this batch's verification and
dropped afterward, as in Phase 2).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings, including after
the `IDatabase::changeIssueStatus` signature change forced updates through `TicketService` and both
adapters, and after the `Domain::Issue`/`CreateIssueRequest` field additions.

### Passing tests (7/7)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-identity-tests`,
   `ticket-hub-authorization-tests`, `ticket-hub-crypto-tests` — unchanged from Phase 1/2, all still
   passing.
2. `ticket-hub-sqlite-integration-tests` — updated: the existing `changeIssueStatus` call to transition
   an issue to `done` now passes a `resolution` (the call would otherwise fail Phase 3's new
   requirement), and asserts the resolution is stored; the deliberately-stale-version call that exercises
   `ConcurrencyConflict` was updated for the new parameter position but is otherwise unchanged.
3. `ticket-hub-workflow-tests` (new) — SQLite, exercised through `TicketService`:
   - hierarchy: a sub-task without a parent is rejected; an epic cannot have a parent; a story's parent
     must be an epic, not another story; a story can link to an epic in the same project; a sub-task's
     parent must be a story/task/bug, not an epic; a parent issue must be in the same project;
   - workflow: transitioning to Done without a resolution is rejected (`Domain::WorkflowViolation`); an
     unknown resolution key is rejected; a valid resolution is stored on completion; reopening does not
     require a resolution and clears the stored one (D70); a parent cannot complete while a sub-task is
     unfinished (D68) and can once the sub-task is done; reopening a completed parent leaves its
     sub-task's status untouched (D69).

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: created a scratch `tickethub` database/role (the server itself needed
  restarting first -- it was not still running from the Phase 2 session), ran `ticket-hub-cli
  migrate`/`seed-demo`, then compiled and ran a standalone program (`pg_phase3_smoke.cpp`, not committed)
  linking directly against `PostgresDatabase`: created an Epic and a Story linked to it (verifying
  `parent_issue_id` round-trips through `createIssue`/`findIssueByKey`), confirmed the Done-without-
  resolution rejection, confirmed a valid resolution is stored and then cleared on reopen, created a
  Sub-task under the Story and confirmed the parent cannot complete while it's unfinished, confirmed
  completing the Sub-task then lets the parent complete, and confirmed reopening the parent afterward
  leaves the Sub-task's status untouched. All 12 assertions passed. The scratch database and role were
  dropped after verification.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all seven test binaries
  compile and link cleanly, including in the SQLite-only and PostgreSQL-only build configurations
  (`-DTICKETHUB_WITH_POSTGRES=OFF` / `-DTICKETHUB_WITH_SQLITE=OFF`).

### Environment limitations (unchanged from Phase 1-2)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com`, needed for CMake `FetchContent` to fetch Crow,
remains blocked. The Phase 3 route changes (`parentIssueKey` on `POST /api/issues`, `resolution` on
`PATCH /api/issues/{key}/status`, `Domain::WorkflowViolation` mapped to HTTP 422, `resolution` added to
issue JSON responses) follow the same patterns as the already-unverified Phase 1/2 routes and carry the
same caveat.

## 2026-07-31 — Phase 2 (authorization and projects, reduced scope)

Verified in the same session/environment as Phase 1 below: GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1,
libpq 16.14, and a live local PostgreSQL 16.14 server (freshly created for this phase's verification and
dropped afterward).

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings, in both the
initial database-layer compile and every subsequent incremental compile as the application layer
(`TicketService`), the new test binary, and the read-access-gating changes were added.

### Passing tests (6/6)

1. `ticket-hub-domain-tests`, `ticket-hub-migration-tests`, `ticket-hub-sqlite-integration-tests`,
   `ticket-hub-identity-tests`, `ticket-hub-crypto-tests` — unchanged from Phase 1, all still passing.
2. `ticket-hub-authorization-tests` (new) — SQLite, exercised through `TicketService` (not the raw
   database adapter, unlike the other integration tests) so the authorization layer itself is under
   test:
   - a project non-member cannot create an issue, change status, or comment in another project
     (`Domain::Forbidden`); a project member can; a project admin (not global admin) can too;
   - not-found semantics are unaffected by authorization: changing the status of an unknown issue still
     returns `false` (not `Forbidden`), commenting on one still raises `std::invalid_argument`;
   - the anonymous-read-access toggle: off by default, an anonymous caller is rejected
     (`Domain::AuthenticationRequired`) while off and admitted once a global administrator turns it on,
     an authenticated caller can always read regardless of the toggle, and only a global administrator
     may flip it;
   - the full project lifecycle authorization matrix: creation is global-admin-only even for another
     project's admin; archiving requires at least project-admin; the recycle bin
     (`listDeletedProjects`/`restoreProject`/`permanentlyDeleteProject`) is global-administrator-only even
     for the project's own admin, matching D88 ("admin restore or permanent delete"); soft-delete
     (moving to the bin) is available to a project admin.
   - One real bug caught by this suite before it was trusted: the test's first assertion expected an
     archived project to still appear (with `archived: true`) in `listProjects()`. The actual (pre-existing,
     correct) behavior is that `listProjects()` is the *active* project list and excludes archived/deleted
     projects entirely, per D87 ("leaves active lists"). The test assertion was wrong, not the
     implementation; it was corrected to assert the project's absence, then its return once unarchived.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server**: created a scratch `tickethub` database/role, ran `ticket-hub-cli
  migrate`/`seed-demo`/`create-user` against it, then compiled and ran a standalone program
  (`pg_phase2_smoke.cpp`, not committed) linking directly against `PostgresDatabase` to exercise every
  Phase 2 method that could not be covered by the SQLite-only `authorization_integration_tests`:
  `getSetting`/`setSetting` (including the `ON CONFLICT` upsert), `findProjectRoleByKey`, `createProject`
  (including the auto-inserted creator-as-admin membership row), `setProjectArchived`,
  `softDeleteProject` (including its idempotent-false second call), `listDeletedProjects` (the `LATERAL`
  join and the on-demand `INTERVAL '90 days'` purge), `restoreProject`, and `permanentlyDeleteProject`
  (including its idempotent-false second call). All 17 assertions passed. The scratch database and role
  were dropped after verification; nothing PostgreSQL-specific was left running.
- Both SQLite and PostgreSQL adapters, `ticket-hub-core`, `ticket-hub-cli`, and all six test binaries
  compile and link cleanly.

### Environment limitations (unchanged from Phase 1)

`src/web/Api.cpp`, `src/web/HttpServer.cpp`, and `src/main.cpp` (the `ticket-hub` server target) still
could not be compiled -- outbound access to `github.com`, needed for CMake `FetchContent` to fetch Crow,
remains blocked. The new Phase 2 routes (`POST/PATCH/DELETE /api/projects/...`,
`GET/PUT /api/settings/anonymous-read`) and the read-route changes to resolve an optional `Principal`
follow the same patterns as the already-unverified Phase 1 routes and carry the same caveat: build and
smoke-test against a real Crow checkout before trusting them.

## 2026-07-31 — Phase 1 (identity and sessions, reduced scope)

Verified in the session environment with GCC 13.3.0, CMake 3.28.3, SQLite 3.45.1, libpq 16.14, and a
live local PostgreSQL 16.14 server.

### Core configuration

```bash
cmake -S . -B build -DTICKETHUB_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

All warnings enabled (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); zero warnings.

### Passing tests (5/5)

1. `ticket-hub-domain-tests` — project/issue key and label validation (unchanged), plus new email
   normalization/validation and password/create-user validation coverage.
2. `ticket-hub-migration-tests` — unchanged (ordered discovery, seed exclusion, stable checksums).
3. `ticket-hub-sqlite-integration-tests` — unchanged issue/comment/status coverage, updated to the
   email/user-id identity model (`assigneeEmail`, real user ids instead of usernames for
   reporter/actor/author).
4. `ticket-hub-identity-tests` (new) — SQLite: seeded-user login (validates the seed's own Argon2id
   hash), session validate/logout, unknown-token/empty-token rejection, identical error for
   wrong-password vs. unknown-email (anti-enumeration check), minimal login-attempt lockout tripping
   and staying locked even against a subsequently-correct password, administrator account creation,
   duplicate-email rejection, invalid-request rejection.
5. `ticket-hub-crypto-tests` (new) — SHA-256 known-answer vectors (empty string, `"abc"`, and the
   55/56/64-byte padding-boundary cases), Argon2id encode/verify round-trip, random-token uniqueness.

### Additional verification beyond the automated suite

- **Live PostgreSQL 16 server** (installed locally in this session, not just SQLite): ran
  `ticket-hub-cli migrate`, `seed-demo`, and `create-user` against a real database, inspected the
  resulting `users`/`local_credentials` schema and rows with `psql`, and ran a standalone program
  exercising `AuthService::login` → `validateSession` → `logout` end-to-end against it. This is strictly
  more PostgreSQL coverage than any prior session recorded in this repository, which previously noted
  "PostgreSQL runtime integration tests require an external test server."
- Confirmed the SQLite migration-runner's foreign-key-safe table-rebuild pattern (`PRAGMA foreign_keys
  OFF` → rebuild → `PRAGMA foreign_keys ON` + `PRAGMA foreign_key_check`) against a minimal standalone
  reproduction before trusting it in `004_identity.sql`, after the first attempt (a plain `ALTER TABLE
  ... DROP COLUMN username`) failed with SQLite's documented "cannot drop UNIQUE column" restriction.
- Both SQLite and PostgreSQL adapters, and the `ticket-hub-cli` target, compile and link cleanly.
- `cmake/FindArgon2.cmake` (new, no upstream CMake package exists for libargon2) resolves correctly via
  `find_package(Argon2 REQUIRED)`.

### Environment limitations

- **The `ticket-hub` server target (Crow) was not compiled.** Outbound HTTPS to `github.com` — needed
  for CMake `FetchContent` to fetch Crow — was blocked by this session's network egress policy (`403`).
  This is the same limitation recorded in every prior session
  (`handoff/IMPLEMENTATION_STATE.md`, the entry below). `src/web/Api.cpp`, `src/web/HttpServer.cpp`, and
  `src/main.cpp` were updated to match the new `AuthService`/`Principal`-based signatures and to add
  `/api/auth/login`, `/api/auth/logout`, `/api/auth/me`, and session-cookie/CSRF protection on existing
  write routes, but **none of this has been compiled or run**. Build and smoke-test the server target
  against a real Crow checkout before trusting it.
- No login page exists in `web/` yet; the demo UI does not call `/api/auth/login`.
- The 15-minute login lockout window could not be tested past the "still locked immediately after
  tripping" case — advancing wall-clock time inside a test is out of scope for this pass.

---

## 2026-07-30 — 0.2.0 baseline (prior session)

Verified in the provided Linux environment with GCC 14.2.0, SQLite 3.46.1 and libpq 17.9.

### Core configuration

```bash
cmake -S . -B build-core \
  -DTICKETHUB_BUILD_SERVER=OFF \
  -DTICKETHUB_WITH_POSTGRES=ON \
  -DTICKETHUB_WITH_SQLITE=ON
cmake --build build-core --parallel 4
ctest --test-dir build-core --output-on-failure
```

### Passing tests

1. `ticket-hub-domain-tests`
   - project key normalization and 2–12-character validation,
   - issue-key normalization and validation,
   - label normalization,
   - issue-create validation.

2. `ticket-hub-migration-tests`
   - ordered discovery,
   - exclusion of demo seed files,
   - stable content checksums.

3. `ticket-hub-sqlite-integration-tests`
   - ordered migration application and idempotence,
   - idempotent demo seed,
   - two projects returned exactly once,
   - eight seeded issues,
   - transactional creation of `TH-7`,
   - labels and detail readback,
   - permanent old-key alias resolution,
   - optimistic status update and version increment,
   - rejection of a stale update,
   - comment creation/listing,
   - dashboard counts and ordering,
   - rejection of a historical migration modified after application.

### Additional static validation

- Both SQLite and PostgreSQL adapters compile and link into `ticket-hub-core`.
- `ticket-hub-cli` compiles, reports version 0.2.0 and applies the schema to a fresh SQLite database.
- SQLite-only and PostgreSQL-only configurations both compile; all tests available in each configuration pass.
- Frontend JavaScript is checked with Node syntax validation when Node is available.

### Environment limitations

- A PostgreSQL server was not available for a live adapter integration test.
- Full Crow server configuration was attempted but could not fetch Crow because the sandbox could not resolve GitHub and no installed Crow package was available. The HTTP-layer source changes therefore were not compiled in this environment.
