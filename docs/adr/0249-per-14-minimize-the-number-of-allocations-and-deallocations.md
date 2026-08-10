# 0249. Per.14: Minimize the number of allocations and deallocations

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Upstream states this rule as a title only — no reason, no example, no
enforcement. There is nothing to transcribe, so this record states the rule and
does not invent the reasoning behind it.

## Decision

Minimise the number of allocations and deallocations.

- Reduce the number of trips to the allocator, rather than the number of bytes
  allocated.

## Rationale

The rule counts calls, not bytes: it is the frequency of allocation and
deallocation that it asks to be reduced, which is the only thing its wording
commits to.

## Consequences

### Positive

- Fewer allocator calls means less of whatever the allocator costs on the target
  in question.

### Negative

- The record rests on a title alone. Upstream gives no reason, so the size of
  the cost being avoided is not stated and cannot be checked against the
  guidelines.
- Reducing allocation count generally means holding memory longer or in larger
  blocks, which the rule does not acknowledge as a cost.
- Applied without measurement it leads straight to the premature optimisation
  [0239](0239-per-2-dont-optimize-prematurely.md) prohibits, and upstream gives
  no threshold at which it starts to apply.

### Neutral

- Minimising allocations is not the same as avoiding dynamic memory; the rule
  does not say to avoid the heap.

## Related

- [0250](0250-per-15-do-not-allocate-on-a-critical-branch.md) — the neighbouring
  rule, also bare upstream, narrowing the same subject to a deadline.
- [0239](0239-per-2-dont-optimize-prematurely.md) — the limit on applying this
  rule before anything has been measured.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the philosophy rule this
  section serves.
