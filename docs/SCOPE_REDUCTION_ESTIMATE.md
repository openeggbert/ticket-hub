# Scope Reduction — Effort Estimate

Status: **finalized**. The questionnaire in `SCOPE_REDUCTION_PROGRESS.md` is complete (142/142
decisions). This file preserves the incremental-tracking history from the questionnaire; the polished,
final version of everything below now lives in **`docs/IMPLEMENTATION_ESTIMATE.md`** — read that file
first. This file is kept for provenance and is not updated further.

## How to read these numbers

- These are **Claude Code agentic execution hours** (an AI coding agent working largely
  autonomously, with review checkpoints), not traditional human developer hours. Claude
  Code hours are typically much lower than human-equivalent hours for well-specified,
  conventional CRUD/backend work, but do not shrink proportionally for work that requires
  many small human decisions, ambiguous UX judgment calls, or external service setup
  (OIDC providers, S3 buckets, SMTP servers) that a human must actually configure.
- Three estimates are tracked per feature/decision and per milestone: **optimistic**,
  **realistic**, **conservative**. Treat all numbers as ranges, not commitments.
- Effort is split into: **implementation**, **tests**, **debugging**, **documentation**,
  **packaging/deployment**, **security hardening**. Many small decisions only touch 1-2 of
  these categories; large ones touch all six.
- Nothing here is final until the questionnaire in `SCOPE_REDUCTION_PROGRESS.md` is
  complete and the final consistency audit has run.

## Running totals (final)

Every one of the 142 decisions in `SCOPE_REDUCTION_PROGRESS.md` carries its own "Est. Hours Saved"
value in that file's table — that is the authoritative per-decision record. Summed:

- **Optimistic (low-bound) sum: ~807h saved** vs. the original full-scope plan.
- **Conservative (high-bound) sum: ~1,346h saved** vs. the original full-scope plan.

See `docs/IMPLEMENTATION_ESTIMATE.md` for the full V1 effort estimate (milestones + category
breakdown) built from this number, plus the implied original-scope/reduction-percentage cross-check.

## Milestone estimates

Final version is in `docs/IMPLEMENTATION_ESTIMATE.md` — Milestone 1 (Minimal usable tracker),
Milestone 2 (Daily personal/team use), Milestone 3 (Public beta), Milestone 4 (Production hardening),
each with optimistic/realistic/conservative Claude Code hour ranges and a
implementation/tests/debugging/documentation/packaging/security category breakdown.

## Known major uncertainty drivers — resolved

The four drivers originally flagged here were resolved during the questionnaire:

- **SQLite parity: kept** (D137) — the user chose to preserve full dual-database feature parity despite
  it being the single largest recurring cost multiplier across dozens of other decisions.
- **OIDC: removed entirely** (D1) — local accounts only, not even an architecture stub.
- **Workflow engine: replaced with one fixed hardcoded workflow** (D4) — this remains, as predicted, the
  single biggest cost reduction in the whole pass (est. 60-100h on that decision alone).
- **Attachment storage: local filesystem only, hardwired** (D15); **email: removed entirely**, both
  outbound and inbound (D14, D52, D115-D123) — together these were the next two largest cost centers,
  exactly as predicted.

See `docs/IMPLEMENTATION_ESTIMATE.md` §"Major uncertainty drivers" for what remains uncertain going
forward.
