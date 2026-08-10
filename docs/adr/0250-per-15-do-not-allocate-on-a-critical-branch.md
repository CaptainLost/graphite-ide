# 0250. Per.15: Do not allocate on a critical branch

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Upstream gives this rule a title and no body — no reason, no example, no
enforcement, and no definition of what makes a branch critical. There is nothing
to transcribe, so this record states the rule as given.

## Decision

Do not allocate on a critical branch.

- Where a path has been identified as critical, keep allocation off it; allocate
  what it needs before it is entered.

## Rationale

The rule is the previous one narrowed to where it matters: it does not ask for
fewer allocations everywhere, only for none on the branch that has been
identified as critical.

## Consequences

### Positive

- A path with no allocation on it does not inherit whatever the allocator's
  worst case happens to be.

### Negative

- The record rests on a title alone. Upstream supplies no reason and no
  definition of "critical branch", so neither the cost being avoided nor the
  scope of the rule can be checked against the guidelines.
- Allocating in advance means committing to a capacity, which is a memory cost
  paid whether or not it is used, and the rule does not weigh that.
- It is nearly unenforceable: no compiler diagnoses "this call may allocate",
  and many allocating constructs do not look like allocation.

### Neutral

- The rule presupposes that a critical branch has been identified. Where none
  has, it imposes nothing.

## Related

- [0249](0249-per-14-minimize-the-number-of-allocations-and-deallocations.md) —
  the broader rule this one narrows, also bare upstream.
- [0240](0240-per-3-dont-optimize-something-thats-not-performance-critical.md) —
  the rule with upstream reasoning about what makes code critical at all.
- [0255](0255-per-30-avoid-context-switches-on-the-critical-path.md) — the other
  bare rule about the same path.
