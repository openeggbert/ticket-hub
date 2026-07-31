# Ticket Hub V1 implementation estimate

Status: final, produced at the end of the 2026-07-31 scope-reduction pass.

## How to read these numbers

- These are **Claude Code agentic execution hours** — an AI coding agent working largely autonomously
  with periodic human review — not traditional human-developer hours. They are typically much lower than
  human-equivalent hours for well-specified, conventional backend/CRUD work, but do **not** shrink
  proportionally for work that needs many small human judgment calls, external service setup, or genuine
  UX design decisions.
- Every number is a **rough order-of-magnitude range**, not a commitment. Three confidence levels are
  given throughout: **optimistic**, **realistic**, **conservative**.
- Two independent estimation methods were used and cross-checked against each other (see "Cross-check"
  below): (1) a bottom-up per-phase estimate of what V1 actually costs to build, and (2) a bottom-up sum
  of the per-decision "hours saved vs. the original full-scope plan" recorded during the questionnaire
  in `docs/SCOPE_REDUCTION_PROGRESS.md`. They agree to within the size of their own uncertainty, which is
  some reassurance but not proof — both rely on the same underlying judgment calls made during the
  questionnaire.

## Milestone estimates

| Milestone | Phases (see `docs/REDUCED_SCOPE_ROADMAP.md`) | Optimistic | Realistic | Conservative |
|---|---|---|---|---|
| 1 — Minimal usable tracker | 1-3 (identity, authorization, issue core) | 57h | 95h | 155h |
| 2 — Daily personal/team use | 4-5 (collaboration, attachments, board) | 38h | 65h | 105h |
| 3 — Public beta | 6-7 (API, backup/restore, upgrade) | 27h | 45h | 75h |
| 4 — Production hardening | 8 (packaging, hardening, docs) | 12h | 20h | 35h |
| **Total V1** | 1-8 | **~134h** | **~225h** | **~370h** |

Milestone 1 carries the most inherent uncertainty (it's the first real vertical slice through an
authorization + fixed-workflow model that doesn't exist yet in the prototype); later milestones build on
proven patterns and narrow faster.

## V1 effort by category

Approximate share of the **realistic** ~225h total; optimistic/conservative scale roughly with the
milestone totals above.

| Category | Optimistic | Realistic | Conservative | Notes |
|---|---|---|---|---|
| Implementation | 75h | 125h | 205h | Domain/application/infra code, migrations, routes, JS |
| Tests | 27h | 45h | 75h | Unit, migration, SQLite/PostgreSQL integration tests |
| Debugging | 13h | 22h | 37h | Fixing failures surfaced by the above, cross-DB issues |
| Documentation | 8h | 14h | 22h | README/NEXT/CHANGELOG/SCHEMA/SCOPE/VERIFICATION updates |
| Packaging and deployment | 5h | 9h | 16h | Dockerfile, Compose, CLI upgrade/backup commands |
| Security hardening | 6h | 10h | 15h | Argon2id/session/CSRF review, rate limiting, dependency review |
| **Total** | **~134h** | **~225h** | **~370h** | |

## Cross-check: sum of per-decision savings

`docs/SCOPE_REDUCTION_PROGRESS.md` records an "hours saved vs. original" estimate for each of the 142
decisions individually (many are `0h` where a decision was already the cheapest option, or explicitly
`"already counted in Decision N"` to avoid double-counting a saving across related decisions). Summing
every decision's low/high bound gives:

- **Low bound sum: ~807h saved.**
- **High bound sum: ~1,346h saved.**

Combined with the V1 totals above (`V1 effort = original effort − savings`), this implies:

| | Optimistic | Realistic | Conservative |
|---|---|---|---|
| V1 effort (this document) | 134h | 225h | 370h |
| Estimated savings (sum of per-decision deltas) | 807h | ~1,076h | 1,346h |
| **Implied original full-scope effort** | ~941h | ~1,301h | ~1,716h |
| **Implied reduction** | **~86%** | **~83%** | **~78%** |

Treat "implied original full-scope effort" as a sanity check, not a real independent estimate of what
`SPECIFICATION.md`'s full scope would cost — nobody built the full 14-phase `ROADMAP.md` product to
measure it. It is presented here only because the questionnaire's own per-decision numbers imply it, and
because it is the basis for the "original vs. reduced" comparison the scope-reduction task explicitly
asked for.

## Major uncertainty drivers

- **Dual PostgreSQL/SQLite parity (D137) was kept.** This is the single largest recurring cost
  multiplier still present in V1 — every migration, every test suite, and (had it not been simplified to
  `LIKE`/`ILIKE`, D43) every search feature has to work correctly on both engines. If a future milestone
  ever revisits D137 and drops to one database, expect V1-sized savings again.
- **Milestone 1 (identity/authorization) has no existing prototype code to build from** — the prototype's
  `users.username` column has to become a real UUID/email/handle identity model, and there is currently
  zero authorization enforcement anywhere in the codebase. This is the least de-risked phase.
- **"Simple" fixed-rule replacements for the workflow engine (D68-D70, D77)** still need careful testing
  even though they're hardcoded — a wrong hardcoded rule is just as broken as a wrong configurable one,
  it's only cheaper to *build*, not necessarily cheaper to *get right*.
- **Kept "expensive-relative-to-value" items** (D16 full Markdown toolbar+preview, D59 anonymous access,
  D79 voting, D84 reactions, D99 all 4 attachment preview types, D137 dual-DB) add real cost the user
  chose to keep despite lower-cost alternatives being offered; their combined weight is folded into the
  Milestone 1-2 estimates above but is worth remembering if the schedule slips.

## What is not estimated here

Nothing classified `DEFER_AFTER_V1` or `REMOVE_COMPLETELY` in `docs/REMOVED_AND_DEFERRED_FEATURES.md`
is included in the V1 estimate above. When any deferred feature is eventually picked up, estimate it
fresh at that time — the per-decision numbers in `docs/SCOPE_REDUCTION_PROGRESS.md` are *savings*
relative to the original full design, not standalone future-milestone estimates.
