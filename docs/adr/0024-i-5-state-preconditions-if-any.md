# 0024. I.5: State preconditions (if any)

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Arguments have meaning that might constrain their proper use in the callee.
Consider:

```cpp
double sqrt(double x);
```

Here `x` must be non-negative. The type system cannot easily and naturally
express that, so other means are needed:

```cpp
double sqrt(double x); // x must be non-negative
```

Some preconditions can be expressed as assertions:

```cpp
double sqrt(double x) { Expects(x >= 0); /* ... */ }
```

Ideally that `Expects(x >= 0)` would be part of the interface of `sqrt()`, but
that is not easily done; for now it is placed in the definition.

Prefer a formal specification of requirements, such as `Expects(p);`. If that is
infeasible, use English text in a comment, such as
`// the sequence [p:q) is ordered using <`.

Most member functions have as a precondition that some class invariant holds.
That invariant is established by a constructor and must be re-established on
exit by every member function called from outside the class. It does not need to
be mentioned for each member function.

## Decision

State a function's preconditions wherever it has any that the signature does not
already enforce.

- Prefer expressing the precondition in the type system, so that there is
  nothing left to state.
- Where that is not possible, state it formally as an assertion in the
  definition.
- Where even that is not possible — an ordering requirement over a range, say —
  state it as a comment on the declaration, in precise language.
- Do not restate the class invariant on every member function; it is understood.

## Rationale

An unstated precondition is a trap that only the author knows about, and the
author is not who trips it. Stating it converts an unbounded question — what is
this function willing to accept? — into a bounded one, and does it at the place
where the caller is already looking.

The preference order matters more than the rule. A precondition eliminated by
the type system needs no discipline to maintain; one written as an assertion is
at least checkable and greppable; one written in prose is better than nothing
and no better than that.

## Consequences

### Positive

- Callers can see what is required, at the declaration, without reading the
  implementation.
- Formally stated preconditions are checkable and can be found by tools.
- Thinking about preconditions tends to reveal that some belong in the type,
  which removes them entirely.

### Negative

- A precondition in the definition rather than the declaration is documentation
  in the wrong place: the caller reads the header, and the header does not say
  it. The language offers no better option today.
- Prose preconditions drift from the code, silently, and nothing detects it.
- Cataloguing preconditions on every function is real effort, much of it spent
  on conditions no caller was ever going to violate.

### Neutral

- Which facility expresses the assertion is
  [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md), not this
  record.
- Not enforceable mechanically.

## Enforcement

(Not enforceable.)

## Related

- [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md) — the
  preferred form for the formal case.
- [0026](0026-i-7-state-postconditions.md) — the other half of the contract.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the best
  precondition is one the type system makes unstateable.
