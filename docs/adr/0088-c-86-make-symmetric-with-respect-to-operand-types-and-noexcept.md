# 0088. C.86: Make == symmetric with respect to operand types and noexcept

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Asymmetric treatment of operands is surprising and a source of errors where
conversions are possible. `==` is a fundamental operation, and programmers
should be able to use it without fear of failure.

A free function taking both operands the same way is symmetric by construction:

```cpp
struct X {
    string name;
    int number;
};

bool operator==(const X& a, const X& b) noexcept {
    return a.name == b.name && a.number == b.number;
}
```

A member operator is not:

```cpp
class B {
    string name;
    int number;
    bool operator==(const B& a) const {
        return name == a.name && number == a.number;
    }
    // ...
};
```

`B`'s comparison accepts conversions for its second operand, but not its first.

If a class has a failure state, like `double`'s NaN, there is a temptation to
make a comparison against the failure state throw. The alternative is to make
two failure states compare equal, and any valid state compare false against the
failure state.

This rule applies to all the usual comparison operators: `!=`, `<`, `<=`, `>`
and `>=`.

## Decision

Define comparison as a non-member function taking both operands by `const&`, and
mark it `noexcept`.

- Write `bool operator==(const T&, const T&) noexcept` as a free function in
  `T`'s namespace, or, where member-wise equality is what is wanted, default it
  as a member with `bool operator==(const T&) const = default;`. Do not
  hand-write a non-defaulted member comparison.
- Apply the same treatment to `!=`, `<`, `<=`, `>` and `>=`.
- Do not let a comparison fail. Where the type has a failure state, define what
  comparing against it means — two failure states equal, a failure state unequal
  to any valid state — rather than reporting an error from the operator.
- Where a heterogeneous comparison is genuinely wanted, provide both orderings
  explicitly rather than relying on a conversion that works in one direction
  only.

## Rationale

The asymmetry is not a matter of taste: a member `operator==` allows a user-
defined conversion on its right operand and none on its left, so `x == y` and
`y == x` can compile differently, or one can compile and the other not. Equality
that is not an equivalence relation breaks every algorithm and container that
assumes it is.

`noexcept` matters because there is nowhere sensible to handle a failed
comparison. `==` appears inside sort predicates, hash lookups, and loop
conditions, and its fixed `bool` result leaves nowhere to report an error to
even if a caller were in a position to act on one.

## Consequences

### Positive

- `a == b` and `b == a` mean the same thing, including where conversions are
  available.
- Generic code and containers get the equivalence relation they assume.
- Comparison cannot fail, so it can be used in conditions and predicates without
  a recovery path.

### Negative

- A non-member comparison cannot see private data, so it needs either a public
  accessor or a `friend` declaration, both of which widen the class's surface.
- The rule closes off deliberately heterogeneous comparison designs — comparing
  a `T` against a cheap lookup key without materialising a `T` — unless every
  ordering is written out by hand.
- `noexcept` forces a decision about the failure state that may have no good
  answer: making two NaN-like values compare equal breaks the analogy with
  floating point, and making them unequal breaks reflexivity.

### Neutral

- C++20's defaulted `operator==` and `operator<=>` derive their `noexcept`
  specification from the members, and C++20's rewritten and reversed candidates
  make even a member `operator==` symmetric with respect to conversions —
  `a == b` will consider `b == a`. A defaulted member comparison therefore
  satisfies this rule without the function being written out, and is preferred
  where it fits; the upstream text predates both features, which is why its
  enforcement bullets are phrased against member operators.
- The rule constrains the operator's shape, not what equality should mean for a
  given type.

## Enforcement

- Flag an `operator==()` for which the argument types differ; the same for the
  other comparison operators `!=`, `<`, `<=`, `>` and `>=`.
- Flag member `operator==()`s; the same for the other comparison operators.

## Related

- [0089](0089-c-87-beware-of-on-base-classes.md) — the hierarchy case, where even
  a symmetric comparison does not save you.
- [0075](0075-c-61-a-copy-operation-should-copy.md) — states its postcondition in
  terms of the `==` this record defines.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the implicit
  conversion that creates the asymmetry.
