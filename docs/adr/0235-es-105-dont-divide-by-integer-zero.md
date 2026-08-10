# 0235. ES.105: Don't divide by integer zero

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

The result is undefined and probably a crash. This also applies to `%`.

```cpp
int divide(int a, int b)
{
    // BAD, should be checked (e.g., in a precondition)
    return a / b;
}
```

The guidelines offer two good answers. One is to state the requirement as a
precondition; the other is to use a type for which the operation is defined:

```cpp
int divide(int a, int b)
{
    // good, address via precondition (and replace with contracts once C++ gets them)
    Expects(b != 0);
    return a / b;
}

double divide(double a, double b)
{
    // good, address via using double instead
    return a / b;
}
```

Floating-point division by zero is not undefined: it yields an infinity or a
NaN under IEEE 754, which is a value the caller can test for rather than a
crash. For critical applications that can afford some overhead, use a
range-checked integer or floating-point type.

## Decision

Never let an integer division or remainder run with a zero divisor.

- State `Expects(divisor != 0)` on any function whose parameter is used as an
  integer divisor, and document the requirement in the interface.
- Where the divisor comes from input, configuration or a computation rather than
  from a caller who can be held to a contract, check it and handle the zero case
  as a normal outcome, not as a violated precondition.
- Where the domain is genuinely continuous, use floating point, where division
  by zero is defined and produces a testable value.
- Apply the same treatment to `%`, which has the same requirement and is more
  easily forgotten.
- Do not rely on a hardware trap; the behaviour is undefined, not defined to
  trap.

## Rationale

Division is the one arithmetic operator with a precondition that cannot be
discharged by choosing a wider type. Overflow can be avoided by picking a large
enough type; a zero divisor cannot be engineered away, so it must be either
excluded by contract or handled as a case. That is why this rule reduces to
[0024](0024-i-5-state-preconditions-if-any.md) rather than to a choice of type.

The distinction the record must keep is between the two answers. `Expects` says
"a zero divisor is a caller bug"; a check-and-handle says "a zero divisor is
possible and here is what happens". Choosing the first when the second is true
turns an input problem into undefined behaviour.

## Consequences

### Positive

- The requirement becomes part of the interface rather than a property of the
  implementation nobody wrote down.
- The failure moves from an undefined operation deep in a computation to a check
  at the boundary where the value entered.
- Handling `%` alongside `/` closes the case most often missed.

### Negative

- A precondition on every division is heavy where the divisor is a constant or
  is obviously non-zero, and the checks accumulate in numeric code.
- `Expects` is a GSL facility whose behaviour on violation is a build-time
  configuration; in a build where it is compiled out, the record's protection is
  gone precisely in the configuration that ships.
- Switching to floating point to dodge the problem changes the arithmetic
  wholesale — rounding, exactness, comparison — which is rarely a fair trade for
  a divisor check.
- The remaining case — a zero divisor arriving from input or configuration,
  where no caller can be held to a contract — still needs an error path, and the
  `Expects` phrasing settles nothing about what that path is: terminate, throw,
  or report. The hardest part of the rule is left open.

### Neutral

- Floating-point division by zero is well defined and is not what this rule
  prohibits, though an infinity or NaN propagating through a calculation is its
  own problem.
- Whether a violated precondition terminates or reports is a policy question
  outside this record.

## Enforcement

- Flag division by an integral value that could be zero.

## Related

- [0024](0024-i-5-state-preconditions-if-any.md) — the requirement this rule
  reduces to.
- [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md) — the
  `Expects` spelling used in the example.
- [0233](0233-es-103-dont-overflow.md) — the neighbouring arithmetic rule, and
  the range-checked types both suggest.
