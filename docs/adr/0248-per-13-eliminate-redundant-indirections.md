# 0248. Per.13: Eliminate redundant indirections

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

As with its neighbour, upstream gives this rule a title and no body — no reason,
no example, no enforcement. There is nothing to transcribe, so this record
states the rule and stops there.

## Decision

Eliminate redundant indirections.

- Do not introduce a pointer, a handle, a wrapper, or a dispatch step that is
  not buying something the code needs.

## Rationale

An indirection that buys nothing is a step the machine takes for no reason, and
the rule's placement in the performance section indicates that the step is
counted as a run-time cost.

## Consequences

### Positive

- Data reached without an intervening step is data the code can use directly.

### Negative

- The record rests on a title alone. There is no upstream reasoning to check it
  against, and nothing that says which indirections upstream considered
  redundant.
- Whether an indirection is redundant is a judgement about future needs, and the
  rule offers no test.
- Removing an indirection is not free — it can couple types at compile time and
  cost address stability — and the rule does not weigh that.

### Neutral

- Nothing here argues against indirection that is doing work; polymorphism,
  shared ownership and compilation firewalls are not addressed by the rule.

## Related

- [0247](0247-per-12-eliminate-redundant-aliases.md) — the neighbouring rule,
  also bare upstream.
- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — an
  indirection that is bought deliberately, and what it is bought for.
- [0251](0251-per-16-use-compact-data-structures.md) — the layout rule upstream
  does give a reason for.
