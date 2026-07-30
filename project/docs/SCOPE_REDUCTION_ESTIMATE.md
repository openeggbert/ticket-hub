# Scope Reduction — Effort Estimate

Status: skeleton, will fill in as the questionnaire in `SCOPE_REDUCTION_PROGRESS.md` proceeds.

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

## Running totals

_(Populated incrementally. Each entry below corresponds to one or more decisions in
SCOPE_REDUCTION_PROGRESS.md and captures only the **delta** — hours saved relative to
building the original full-scope feature.)_

| Decision(s) | Feature | Optimistic saved (h) | Realistic saved (h) | Conservative saved (h) | Notes |
|---|---|---|---|---|---|
| _none yet_ | | | | | |

**Cumulative hours saved so far:** optimistic 0h / realistic 0h / conservative 0h.

## Milestone estimates

Not yet computed — depends on the full set of classifications. Will be filled in during
the "Final outputs" phase, structured as:

- Milestone 1 — Minimal usable tracker
- Milestone 2 — Daily personal/team use
- Milestone 3 — Public beta
- Milestone 4 — Production hardening

Each milestone will carry optimistic/realistic/conservative Claude Code hour ranges,
broken down by implementation/tests/debugging/documentation/packaging/security.

## Known major uncertainty drivers

- Whether SQLite parity is dropped or kept materially changes adapter/testing effort
  across almost every phase.
- Whether OIDC is kept at all is a large swing item (provider metadata, JWKS, account
  linking, JIT provisioning).
- The workflow engine (conditions/validators/post-functions/drafts/publishing) is the
  single most expensive subsystem in the original plan; its final classification will
  dominate the total estimate more than any other decision.
- Attachment storage (filesystem + S3, previews, integrity audits) and notifications/email
  (in-app + SMTP + inbound IMAP) are the next two largest cost centers.
