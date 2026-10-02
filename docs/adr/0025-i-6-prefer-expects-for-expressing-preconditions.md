# 0025. I.6: Prefer Expects() for expressing preconditions

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

The reason is to make it clear that a condition is a precondition, and to enable
tool use:

```cpp
int area(int height, int width)
{
    Expects(height > 0 && width > 0);            // good
    if (height <= 0 || width <= 0) my_error();   // obscure
    // ...
}
```

Preconditions can be stated in many ways — comments, `if` statements,
`assert()`. That makes them hard to distinguish from ordinary code, hard to
update, hard to manipulate by tools, and they might have the wrong semantics: do
you always want to abort in debug mode and check nothing in production runs?

Preconditions should be part of the interface rather than part of the
implementation, but the language facilities to do that do not yet exist. Once
language support becomes available — the contract proposal — the standard
version of preconditions, postconditions and assertions will be adopted.

`Expects()` can also be used to check a condition in the middle of an algorithm.

And no, using `unsigned` is not a good way to sidestep the problem of ensuring
that a value is non-negative.

## Decision

Express preconditions with a dedicated precondition facility — `Expects()` or
an equivalent facility — rather than with `assert()`, a bare `if`, or a
comment.

- One condition per statement where practical, so a failure identifies itself.
- Do not mix precondition checks with ordinary error handling: a violated
  precondition is a programming error, not a runtime condition to be recovered
  from.
- Do not change a parameter's type to `unsigned` merely to make a negative value
  unrepresentable; the conversion moves the bug rather than removing it.
- When standard contract support arrives, migrate to it.

## Rationale

The value is in the distinction, not in the check. A precondition and a runtime
error look identical when both are written as `if`, and they call for opposite
responses: one is fixed by changing the caller, the other is handled. Naming the
first kind makes it greppable, tool-visible, and separately configurable.

The `unsigned` note is worth keeping because the trick is so tempting. It makes
`-1` arrive as a very large positive number, which is a worse failure than the
one it was meant to prevent.

## Consequences

### Positive

- Preconditions are distinguishable from logic, at a glance and by tools.
- Whether violations abort, log, or compile away is one policy decision rather
  than a per-site one.
- Migration to language contracts later is a mechanical change.

### Negative

- Whether the check is present in a release build is a build-configuration
  question this record leaves open, so the same code has different behaviour in
  different configurations.
- The precondition sits in the definition, so it is invisible to a caller
  reading the header — the limitation named in
  [0024](0024-i-5-state-preconditions-if-any.md) and not fixed here.
- Adopting a facility means depending on whatever provides it.

### Neutral

- Not mechanically enforceable; finding the variety of ways preconditions can be
  asserted is not feasible, and warning about `assert()` has questionable value
  without a language facility.
- `Expects()` is also usable mid-algorithm, so this record does not restrict it
  to function entry.

## Enforcement

(Not enforceable.) Finding the variety of ways preconditions can be asserted is
not feasible. Warning about those that can be easily identified — `assert()` —
has questionable value in the absence of a language facility.

## Related

- [0024](0024-i-5-state-preconditions-if-any.md) — the rule this one gives a
  form to.
- [0027](0027-i-8-prefer-ensures-for-expressing-postconditions.md) — the
  symmetric decision for postconditions.
- [0013](0013-p-7-catch-run-time-errors-early.md) — checking early, of which a
  precondition is the clearest case.
