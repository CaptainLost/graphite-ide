# 0126. C.161: Use non-member functions for symmetric operators

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

If you use member functions you need two of them. Unless a non-member function
is used for, say, `==`, then `a == b` and `b == a` will be subtly different.

```cpp
bool operator==(Point a, Point b) { return a.x == b.x && a.y == b.y; }
```

The asymmetry comes from how the two operands are treated. A member
`operator==` has an implicit object argument, and a user-defined conversion is
never applied to it: the left operand must already be a `Point`, or something
derived from one, while the right operand may be anything convertible to one.
So with a converting constructor in play, `p == q` compiles and `q == p` does
not, or the two select different overloads. A non-member takes both operands as
ordinary parameters, so both are subject to the same conversions and the same
overload resolution.

C++20 narrows the problem for the comparison operators specifically: `q == p`
now also considers the reversed candidate `p == q`, so a member `operator==`
is much less asymmetric than it was under C++17. No such rewriting exists for
`+`, `-`, `*`, `/`, `%` or the bitwise operators, where the argument above
still holds in full.

## Decision

Define an operator whose operands play symmetric roles as a non-member function.

- Define `==`, `!=`, `<`, `<=`, `>`, `>=`, `<=>`, `+`, `-`, `*`, `/`, `%` and
  the bitwise binary operators as non-members.
- Take both operands the same way — both by value for small types, both by
  `const` reference otherwise — so neither side is privileged.
- Give the operator access to the representation by declaring it a friend, or by
  implementing it in terms of the public interface where that is enough.
- Keep as members the operators that are inherently asymmetric because they act
  on the left operand: `=`, the compound assignments, `[]`, `()`, `->` and the
  unary operators.
- Define the non-member in the namespace of its operands, per
  [0133](0133-c-168-define-overloaded-operators-in-the-namespace-of-their-operands.md).

## Rationale

The rule is usually explained as needing two member functions where one
non-member would do, but the sharper problem is that the two are not
interchangeable even when both are written. The implicit object parameter is
outside the conversion machinery, so a member operator is not merely
inconvenient — it means something different from its mirror image, and the
difference appears only when a conversion is involved, which is exactly when
nobody is looking.

## Consequences

### Positive

- `a == b` and `b == a` resolve identically, including through user-defined
  conversions.
- One definition covers both directions instead of two that must be kept in
  step.
- Non-members participate in argument-dependent lookup, so the operator is found
  from generic code without qualification.

### Negative

- A non-member usually needs `friend` to reach the representation, which
  reintroduces the coupling that making it a non-member was supposed to reduce.
- It cannot be virtual, so a symmetric operator over a hierarchy needs a
  non-member forwarding to a virtual function — more machinery than a member
  would have been.
- Free operators are easy to declare far from the type; keeping them next to it
  is a discipline the language does not enforce.
- The rule as stated would flag member `operator==` on types that have no
  converting constructors, where the asymmetry cannot arise and the member form
  is perfectly safe.

### Neutral

- C++20 rewriting makes `a != b` and the reversed comparison candidates
  automatic from a single `==`, which reduces the number of operators to write
  but does not change where they should be defined.
- A hidden friend, defined inline in the class body, satisfies this rule and is
  found only by argument-dependent lookup; whether to prefer that form is a
  separate question.

## Enforcement

- Flag member operator functions.

## Related

- [0125](0125-c-160-define-operators-primarily-to-mimic-conventional-usage.md) —
  requires that a binary operator treat its operands equivalently, which this
  rule implements.
- [0133](0133-c-168-define-overloaded-operators-in-the-namespace-of-their-operands.md)
  — where the resulting non-member belongs.
- [0129](0129-c-164-avoid-implicit-conversion-operators.md) — the conversions
  that expose the member form's asymmetry.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — a symmetric
  operation should have a symmetric signature.
