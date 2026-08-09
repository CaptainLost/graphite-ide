# 0024. I.5: State preconditions (if any)

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

This codebase states preconditions with `CORE_ASSERT`, which is active in Debug
only, and with doc comments. `Result<T>::Value()` asserting that the result
holds a value is the clearest example.

## Decision

Adopt I.5 as written. Transcribed in full:

> **Reason.** Arguments have meaning that might constrain their proper use in
> the callee.

**Example.** Consider:

```cpp
double sqrt(double x);
```

Here `x` must be non-negative. The type system cannot (easily and naturally)
express that, so we must use other means. For example:

```cpp
double sqrt(double x); // x must be non-negative
```

Some preconditions can be expressed as assertions. For example:

```cpp
double sqrt(double x) { Expects(x >= 0); /* ... */ }
```

Ideally, that `Expects(x >= 0)` should be part of the interface of `sqrt()` but
that's not easily done. For now, we place it in the definition (function body).

References: `Expects()` is described in GSL.

**Note.** Prefer a formal specification of requirements, such as `Expects(p);`.
If that is infeasible, use English text in comments, such as
`// the sequence [p:q) is ordered using <`.

**Note.** Most member functions have as a precondition that some class invariant
holds. That invariant is established by a constructor and must be reestablished
upon exit by every member function called from outside the class. We don't need
to mention it for each member function.

**Enforcement.** (Not enforceable)

## Rationale

Adopted as written, with `CORE_ASSERT` standing in for `Expects()` — the GSL is
declined by [0019](0019-p-13-use-support-libraries-as-appropriate.md), and
`CORE_ASSERT` already carries a message and fires only in Debug.

The second Note matters more than it looks: it licenses *not* restating the
class invariant on every member function, which is why the headers in `Core/Sln`
document the invariant once on the type rather than on each method.

The distinction this rule leans on is the one
[0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md)
already draws: a precondition is the caller's obligation and gets an assert; an
environmental failure gets an `Error`.

## Consequences

### Positive

- Matches existing practice and gives it a name, so the assert-versus-`Result`
  choice has a written basis rather than being decided case by case.

### Negative

- `CORE_ASSERT` compiles away outside Debug, so a violated precondition in
  Release is undefined behaviour rather than a diagnostic. The rule's preference
  for a formal, checkable form is only half met.
- Preconditions stated in comments are not checked at all and go stale without
  anything noticing.

### Neutral

- The rule is explicitly not enforceable; nothing will ever verify this
  mechanically.

## Related

- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the previous
  rule.
- [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md) — the next
  rule, on the form.
- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  assert versus `Error`.
