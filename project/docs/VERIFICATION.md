# Verification record

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
