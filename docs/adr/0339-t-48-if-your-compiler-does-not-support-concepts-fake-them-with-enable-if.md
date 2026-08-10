# 0339. T.48: If your compiler does not support concepts, fake them with enable_if

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

The reason given is simply that it is the best that can be done without direct
concept support: `enable_if` can be used to define functions conditionally and to
select among a set of functions.

```cpp
template<typename T>
enable_if_t<is_integral_v<T>>
f(T v)
{
    // ...
}

// Equivalent to:
template<Integral T>
void f(T v)
{
    // ...
}
```

Beware of complementary constraints. Faking concept overloading with `enable_if`
sometimes forces the use of that error-prone design technique, which
[0330](0330-t-25-avoid-complementary-constraints.md) rules out.

Upstream gives no enforcement for this rule.

The rule is conditional, and its condition is historical. C++20 made concepts a
language feature, so on any toolchain implementing it the premise — a compiler
without concept support — does not hold, and the fallback the rule describes is
not the best that can be done.

## Decision

Write real constraints wherever the language provides them; simulate them only
where it does not.

- Constrain templates with concepts, `requires` clauses and constrained
  parameters, per
  [0321](0321-t-10-specify-concepts-for-all-template-arguments.md).
- Do not introduce new `std::enable_if` machinery into a signature to express a
  constraint that a `requires` clause expresses directly.
- Where `enable_if` appears in existing or third-party code, read it as the
  constraint it encodes, and convert it when that code is being changed anyway.
- Where a property is genuinely easier to compute as a trait — by partial
  specialisation, say — keep the trait, but put it behind a named concept so the
  signature still reads as a constraint, as
  [0329](0329-t-24-use-tag-classes-or-traits-to-differentiate-concepts-that-differ-only-in-semantics.md)
  does.
- Where the toolchain has no concepts, follow the rule as written, and take its
  warning about complementary constraints seriously rather than as an aside.

## Rationale

The rule is a compatibility measure, and its own text presents `enable_if` as the
best available rather than as good: it moves the constraint into the return type,
it produces diagnostics that name substitution failures instead of unmet
requirements, and it pushes authors towards the complementary-constraint pattern
that the guidelines separately forbid. None of that has to be paid where concepts
exist, so the rule's scope shrinks to the toolchains that lack them.

## Consequences

### Positive

- Constraints are stated in the signature, where a reader looks, instead of being
  encoded in a return type.
- Diagnostics name the unsatisfied concept.
- The pressure towards complementary constraints disappears, since constrained
  overloads are ordered by subsumption.

### Negative

- The rule cannot simply be discarded: `enable_if` remains in the standard
  library and in existing code, so it must still be read, and a reader has to
  know why new code does not write it.
- Concepts do not displace traits: a concept is usually defined in terms of the
  same type traits, so `enable_if` leaves the signatures while the trait
  machinery behind it stays, and a reader still needs both vocabularies.
- Converting `enable_if` code to concepts changes overload resolution subtly, so
  a mechanical rewrite of working code is a risk rather than a tidy-up.
- Code that must build under both a C++20 and a pre-C++20 toolchain has to carry
  both forms and keep them in agreement, which is worse than either alone.

### Neutral

- On a toolchain without concept support the rule applies exactly as written; it
  is transcribed so that the fallback and its hazards are on record.
- [0008](0008-p-2-write-in-iso-standard-c.md) is satisfied either way:
  `enable_if` and concepts are both standard, and the choice is about which
  standard level is targeted.

## Related

- [0008](0008-p-2-write-in-iso-standard-c.md) — both forms are ISO C++; the
  difference is the version.
- [0321](0321-t-10-specify-concepts-for-all-template-arguments.md) — what is
  written where concepts exist.
- [0330](0330-t-25-avoid-complementary-constraints.md) — the design technique
  `enable_if` drags in.
- [0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md) — its
  enforcement also looks for `enable_if` simulating single-operation concepts.
