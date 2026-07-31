# Ticket Hub next work

Current version: 0.2.0
Current roadmap: **reduced-scope V1** — see `REDUCED_SCOPE_SPECIFICATION.md` and
`docs/REDUCED_SCOPE_ROADMAP.md`. `SPECIFICATION.md` and `docs/ROADMAP.md` are kept as the long-term
aspirational baseline but are **not** the current build target.

Scope was re-reviewed decision-by-decision with the product owner on 2026-07-31 (142/142 decisions;
see `docs/REDUCED_SCOPE_DECISIONS.md` and `docs/REMOVED_AND_DEFERRED_FEATURES.md`). Do not implement
anything from the removed/deferred list without an explicit new product conversation.

## Completed so far

- Approved reduced-scope V1 specification, architecture, data model, roadmap, and effort estimate
  (this scope-reduction pass).
- Prior batch: ordered checksummed migrations with PostgreSQL advisory locking, separate demo seed,
  issue optimistic-lock version and HTTP conflict foundation, project/issue key alias and recycle-bin
  schema foundations, key/label normalization, administration CLI (version/diagnostics/migrate/
  seed-demo), domain/migration/SQLite integration tests, SQLite-only and PostgreSQL-only build
  verification.

## Immediate next implementation slice — Phase 1 of `docs/REDUCED_SCOPE_ROADMAP.md`

**Identity and sessions** (Milestone 1 — Minimal usable tracker), **further reduced by the product
owner on 2026-07-31 before implementation started** (see `docs/REDUCED_SCOPE_ROADMAP.md` Phase 1 for
the full rationale). This is the smallest phase that still produces forward progress: nothing else in
the reduced roadmap can start until real principals exist.

1. Introduce a `Principal` model and pass it into application write use cases, **removing the fixed
   `demo` user entirely** (no dual demo-mode toggle — simpler than the original plan).
2. Add identity migrations for the **further-reduced** scope only:
   - `users`: UUID identity, unique email, `time_zone`, `clock_format` (migrate the prototype's
     `username` column) — **no `handle` column yet** (added in Phase 4 with @mentions) and **no**
     generic i18n `locale` field,
   - `local_credentials` (Argon2id),
   - `sessions`.
   - Do **not** add `invitations`, `groups`/`group_members`, `oidc_providers`, or
     `external_identities` — all removed for V1 (`docs/REDUCED_SCOPE_DATA_MODEL.md` §B).
3. Add local-password hashing behind an authentication port: Argon2id, basic password length/strength
   check, and a simple **login attempt counter** — the full configurable lockout policy is deferred to
   Phase 6 alongside REST rate limiting (D124).
4. Add login/logout/session endpoints and CSRF protection. **No active-session list or "sign out
   everywhere" endpoint yet** — deferred to Phase 6.
5. Add an administrator-only "create user" action that **sets the password directly** — no
   `must_change_password` flag, no forced-change-on-first-login flow (permanent V1 simplification, not
   a resequencing). This is the entire registration/reset story for V1; there is no invitation flow and
   no self-service email reset (see `REDUCED_SCOPE_SPECIFICATION.md` §3).
6. Add authentication and session integration tests for SQLite; add PostgreSQL test wiring through an
   environment-provided connection string.

Exit gate (per `docs/REDUCED_SCOPE_ROADMAP.md` Phase 1): no fixed demo identity remains anywhere in the
codebase; login/logout/session endpoints are tested on both databases; `ctest --output-on-failure`
green on all supported build configurations.

## After Phase 1

Continue in order through `docs/REDUCED_SCOPE_ROADMAP.md`: Phase 2 (authorization and projects),
Phase 3 (issue core and the fixed workflow), then Milestone 2 (collaboration, attachments, Kanban
board), Milestone 3 (API, backup/restore), Milestone 4 (packaging and hardening). Do not jump ahead to
later-phase features early, and do not implement anything from
`docs/REMOVED_AND_DEFERRED_FEATURES.md`.

## Known verification limitation

The Crow server target could not be configured in the sandbox because the environment could not
resolve GitHub and no installed Crow package was available. Core, CLI, SQLite-only, and
PostgreSQL-only targets compile successfully. Re-verify this at the start of Phase 1 in the actual
implementation environment before assuming it still applies.
