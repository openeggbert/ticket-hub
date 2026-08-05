# Product decisions status

The interactive product-definition pass is complete, and it was **re-reviewed and reduced in scope**
on 2026-07-31 (142/142 decisions re-classified for a smaller, finishable V1). The current authoritative
decisions are consolidated in [../REDUCED_SCOPE_SPECIFICATION.md](../REDUCED_SCOPE_SPECIFICATION.md),
with the full V1 ledger in [REDUCED_SCOPE_DECISIONS.md](REDUCED_SCOPE_DECISIONS.md) and the complete
question-by-question record in [SCOPE_REDUCTION_PROGRESS.md](SCOPE_REDUCTION_PROGRESS.md).

The original full-scope decisions remain in [../SPECIFICATION.md](../SPECIFICATION.md) and
[PRODUCT_DECISIONS_COMPLETE.md](PRODUCT_DECISIONS_COMPLETE.md) as the long-term aspirational baseline
for features listed in [REMOVED_AND_DEFERRED_FEATURES.md](REMOVED_AND_DEFERRED_FEATURES.md) — do not
build from those two files directly without checking whether the feature in question survived the
reduction.

No unresolved product question blocks the reduced-scope Phase 1 (see `../NEXT.md`). New decisions
should be recorded as an ADR only when implementation uncovers a genuine contradiction, security issue,
or unavoidable tradeoff. Minor UI wording and numeric defaults should use conservative documented
defaults rather than reopening product scope. Reopening anything in
`REMOVED_AND_DEFERRED_FEATURES.md` requires an explicit new product conversation with the owner, not a
unilateral implementation choice.
