# 0089. C.87: Beware of == on base classes

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

It is really hard to write a foolproof and useful `==` for a hierarchy.

```cpp
class B {
    string name;
    int number;
    virtual bool operator==(const B& a) const
    {
         return name == a.name && number == a.number;
    }
    // ...
};
```

`B`'s comparison accepts conversions for its second operand, but not its first.
Deriving from it makes the problem worse rather than better:

```cpp
class D : B {
    char character;
    virtual bool operator==(const D& a) const
    {
        return name == a.name && number == a.number && character == a.character;
    }
    // ...
};

B b = ...
D d = ...
b == d;    // compares name and number, ignores d's character
d == b;    // error: no == defined
D d2;
d == d2;   // compares name, number, and character
B& b2 = d2;
b2 == d;   // compares name and number, ignores d2's and d's character
```

The same two objects compare differently depending on the static type of the
reference through which they are reached, and one of the orderings does not
compile at all. Of course there are ways of making `==` work in a hierarchy, but
the naive approaches do not scale.

This rule applies to all the usual comparison operators: `!=`, `<`, `<=`, `>`,
`>=` and `<=>`.

## Decision

Do not put a virtual comparison operator on a base class.

- Do not declare `operator==` — or any of `!=`, `<`, `<=`, `>`, `>=`, `<=>` —
  virtual.
- Do not define a comparison on a class intended as a base for a polymorphic
  hierarchy; a type with a value comparison and a type with polymorphic
  behaviour should generally be different types.
- Where objects in a hierarchy must be compared, provide a named virtual
  function — an `equals(const B&)` that first checks the dynamic types agree and
  then compares — and let callers use it deliberately.
- Do not attempt to fix the problem by overloading `operator==` in the derived
  class; that produces the asymmetric, static-type-dependent behaviour shown
  above.
- Where a hierarchy exists only to hold values, prefer a closed set — a variant
  or a tagged union — which can be compared symmetrically.

## Rationale

The example's last two lines are the argument. `d == d2` and `b2 == d`, with
`b2` bound to `d2`, compare the same pair of objects and give different answers,
because the operator chosen depends on the static type of the expression while
the objects do not. No naming discipline or extra override fixes this: the
language picks the operator before it knows what the objects are.

Equality also has obligations — reflexivity, symmetry, transitivity — that a
hierarchy cannot satisfy in general, since a base and a derived object comparing
equal to the same third object need not compare equal to each other.

## Consequences

### Positive

- The static-type-dependent comparisons and the missing reverse ordering cannot
  arise, because the operator does not exist.
- Containers and algorithms are not offered an `==` that fails to be an
  equivalence relation.
- Code that needs to compare polymorphic objects has to say what it means by
  that, at the call site.

### Negative

- Polymorphic objects genuinely do sometimes need comparing — deduplicating a
  list of heterogeneous nodes, for instance — and the named-function workaround
  is more verbose and is not found by any generic code that uses `==`.
- A hand-written `equals` still has to compare dynamic types before comparing
  state, and every derived class must remember to extend it; the obligation
  moves rather than disappearing.
- Restructuring a value hierarchy into a variant is a large change, and is not
  always possible when the set of types is open by design.

### Neutral

- The rule is a warning rather than an outright prohibition on comparing objects
  in a hierarchy; what it forbids is doing so through a virtual operator.
- C++20's `<=>` does not change the analysis: it is covered by the same rule.

## Enforcement

- Flag a virtual `operator==()`; the same for the other comparison operators
  `!=`, `<`, `<=`, `>`, `>=` and `<=>`.

## Related

- [0088](0088-c-86-make-symmetric-with-respect-to-operand-types-and-noexcept.md)
  — the symmetry requirement that a hierarchy cannot meet.
- [0081](0081-c-67-a-polymorphic-class-should-suppress-public-copy-move.md) — the
  same separation of value types from polymorphic types, applied to copying.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — an interface with no state has nothing to compare.
- [0075](0075-c-61-a-copy-operation-should-copy.md) — copy and comparison have to
  agree, which is another thing a hierarchy makes hard.
