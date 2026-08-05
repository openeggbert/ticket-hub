# Handoff notes for the next coding agent

> **2026-07-31 scope-reduction amendment:** the product owner re-reviewed all 142 decisions in
> `PRODUCT_DECISIONS_COMPLETE.md` and produced a smaller V1 target. Read
> `../REDUCED_SCOPE_SPECIFICATION.md`, `REDUCED_SCOPE_DECISIONS.md`, and `REDUCED_SCOPE_ROADMAP.md`
> first — they now take precedence over this file and over `SPECIFICATION.md` for what to build.
> In particular, the "Immediate implementation advice" below is superseded where it conflicts:
> **OIDC, invitations, and groups are permanently removed for V1**, not deferred to "follow as separate
> vertical slices" as originally written — see `REMOVED_AND_DEFERRED_FEATURES.md`. PATs still exist but
> without scopes/rotation/service accounts (D39, D40). Permission schemes are replaced by fixed project
> roles (D3), not deferred.

## Original request

Build a self-hosted Jira-like tracker named **Ticket Hub** in C++ using Crow and vanilla HTML/CSS/JavaScript. The primary namespace is `TicketHub`. PostgreSQL is the primary database, with SQLite optional through an architecture that permits different database implementations.

The user then completed an extensive interactive scope pass. All final choices are in:

- `../SPECIFICATION.md` — consolidated normative behavior.
- `PRODUCT_DECISIONS_COMPLETE.md` — chronological numbered decision ledger.

This chat is expected to be deleted, so do not depend on conversation history outside the repository.

## Current code lineage

1. Initial functional vertical-slice prototype.
2. Version 0.2.0 foundation pass:
   - checksummed ordered migrations,
   - PostgreSQL migration lock,
   - CLI,
   - issue optimistic-lock foundation,
   - key aliases,
   - recycle-bin foundations,
   - normalization fixes,
   - expanded documentation/tests.
3. Final handoff pass added documentation only (`CLAUDE.md`, this file, and the complete decision register).

Original archives are preserved one directory above the project in the complete handoff package.

## Important resolved deviations from Jira

Ticket Hub is intentionally Jira-like, not a byte-for-byte clone. The user deliberately chose:

- no JQL initially,
- no Jira screen/screen-scheme system,
- fixed initial priorities, resolutions, and issue types,
- exactly one component per issue,
- exactly one workflow status per board column,
- no issue-level security schemes,
- one fixed personal dashboard,
- no SLA,
- no recurring issues,
- no threaded comments,
- Markdown-only checklists,
- no attachment deduplication,
- no full native Git provider integration initially.

Do not “correct” these choices merely because Jira behaves differently.

## Immediate implementation advice

The next change should not attempt OIDC, permissions, and all auth features at once. A safe first vertical slice is:

1. Add `Principal` with anonymous/authenticated/system/service-account kinds and immutable user ID when applicable.
2. Pass principal through application commands and remove hard-coded demo actor IDs.
3. Add identity schema migration for users and local credentials with inactive/anonymized lifecycle fields.
4. Add repositories and a local-login/session service boundary.
5. Implement session creation/lookup/revocation with secure random tokens and hashed verifier storage.
6. Add Crow authentication middleware if Crow is available; otherwise unit/integration-test the core first.
7. Keep demo seed compatible by creating a known demo user, but never make production code silently assume it.
8. Update all mutation history to use the principal actor.

Registration/invitations, OIDC, groups, PATs, and permission schemes can follow as separate tested vertical slices.

## Naming guidance

Use namespaces by responsibility, for example:

- `TicketHub::Domain`
- `TicketHub::Application`
- `TicketHub::Infrastructure::Database`
- `TicketHub::Infrastructure::Auth`
- `TicketHub::Web`

Avoid a monolithic `Utils` namespace and avoid database table objects becoming domain entities by accident.

## API status

The existing prototype routes are unversioned. The approved public API is `/api/v1`. Do not promise compatibility for the prototype endpoints. When authentication work begins, decide whether to introduce `/api/v1` immediately or keep a clearly marked demo API until the first stable vertical slice; document the choice.

## Known verification limitation

The source environment lacked network access to fetch Crow and lacked an installed Crow package, so the full HTTP target was not compiled there. Core, CLI, SQLite, and PostgreSQL adapter compilation succeeded. Re-run the server build in an environment with Crow before trusting route-level changes.
