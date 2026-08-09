# 0025. I.6: Prefer Expects() for expressing preconditions

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

I.5 says to state preconditions; I.6 says in what form. `Expects()` comes from
the GSL, which this project declines, so this record is about what stands in its
place.

## Decision

Adopt I.6 in substance, with `CORE_ASSERT` in place of `Expects()`. Transcribed
in full:

> **Reason.** To make it clear that the condition is a precondition and to enable
> tool use.

**Example.**

```cpp
int area(int height, int width)
{
    Expects(height > 0 && width > 0);            // good
    if (height <= 0 || width <= 0) my_error();   // obscure
    // ...
}
```

**Note.** Preconditions can be stated in many ways, including comments,
if-statements, and `assert()`. This can make them hard to distinguish from
ordinary code, hard to update, hard to manipulate by tools, and might have the
wrong semantics (do you always want to abort in debug mode and check nothing in
productions runs?).

**Note.** Preconditions should be part of the interface rather than part of the
implementation, but we don't yet have the language facilities to do that. Once
language support becomes available (e.g., see the contract proposal) we will
adopt the standard version of preconditions, postconditions, and assertions.

**Note.** `Expects()` can also be used to check a condition in the middle of an
algorithm.

**Note.** No, using `unsigned` is not a good way to sidestep the problem of
ensuring that a value is non-negative.

**Enforcement.** (Not enforceable) Finding the variety of ways preconditions can
be asserted is not feasible. Warning about those that can be easily identified
(`assert()`) has questionable value in the absence of a language facility.

## Decision detail

A precondition is written as `CORE_ASSERT(condition, "message")` at the top of
the function body. It is not written as an `if` that returns an `Error`: a
precondition is the caller's obligation, and turning it into a recoverable
failure hides a programming mistake behind an error path the caller will
plausibly ignore.

## Rationale

Adopted in substance because the rule's goal — a precondition that is
distinguishable from ordinary control flow — is met by `CORE_ASSERT`, which is
named for the purpose and carries a message.

The first Note is the rule's own admission of the weakness this project
inherits: `CORE_ASSERT` does abort in Debug and check nothing in Release, which
is precisely the semantics the Note questions. Adopting the rule does not make
that go away; it names it.

The fourth Note is worth keeping visible, because `unsigned` parameters are a
recurring temptation in this codebase's index arithmetic and the rule rejects
that dodge explicitly.

## Consequences

### Positive

- One named form for preconditions, distinguishable at a glance from an `if`
  that handles a real failure.
- Draws the line between assert and `Result<T>` in a way that can be applied
  without re-arguing it.

### Negative

- `CORE_ASSERT` is not `Expects()`: no tool reads it, and it disappears entirely
  outside Debug. The rule's "enable tool use" reason is unmet.
- Placing the assert in the body rather than the declaration means a caller
  reading only the header does not see the obligation, which the rule
  acknowledges as a language limitation and this project inherits.

### Neutral

- The contract proposal the second Note anticipates is not in C++20, so there is
  nothing to migrate to yet.

## Related

- [0024](0024-i-5-state-preconditions-if-any.md) — the previous rule.
- [0026](0026-i-7-state-postconditions.md) — the next rule.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — why `Expects()`
  itself is unavailable.
