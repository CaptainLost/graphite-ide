# 0247. Per.12: Eliminate redundant aliases

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Upstream gives this rule a title and nothing else — no reason, no example, no
enforcement. There is nothing to transcribe, so this record records the rule as
stated and does not supply reasoning the guidelines do not give.

## Decision

Eliminate redundant aliases.

- Do not introduce a second name — a reference, a pointer, or a type alias — for
  something that already has one that serves the same purpose.

## Rationale

An alias that duplicates an existing name adds nothing to what the code can
express, and the rule's placement in the performance section indicates that the
cost it has in mind is a run-time one.

## Consequences

### Positive

- Fewer live names for one object, which is one fewer thing to keep track of
  when reading.

### Negative

- The record rests on a title alone. There is no upstream reasoning to check it
  against, and no example that shows what upstream counted as redundant.
- "Redundant" is undefined here, so the rule cannot be applied without a
  judgement it does not supply.
- It is unenforceable: nothing distinguishes a redundant alias from a useful one
  mechanically.

### Neutral

- Whether the rule is about object aliasing or about `using` declarations is not
  recoverable from the source.

## Related

- [0248](0248-per-13-eliminate-redundant-indirections.md) — the neighbouring
  rule, equally bare upstream.
- [0245](0245-per-10-rely-on-the-static-type-system.md) — types are what the
  optimiser uses to prove two names cannot alias.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the philosophy rule this
  section serves.
