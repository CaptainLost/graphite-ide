# 0044. C.5: Place helper functions in the same namespace as the class they support

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A helper function is a function, usually supplied by the writer of a class, that
does not need direct access to the representation of the class yet is seen as
part of the useful interface to it. Placing helpers in the same namespace as the
class makes their relationship to the class obvious, and allows them to be found
by argument-dependent lookup.

```cpp
namespace Chrono { // here we keep time-related services
    class Time { /* ... */ };
    class Date { /* ... */ };

    // helper functions:
    bool operator==(Date, Date);
    Date next_weekday(Date);
    // ...
}
```

A caller writing `next_weekday(d)` or `d1 == d2` finds these without naming
`Chrono`, because the argument's own namespace is searched. This is especially
important for overloaded operators, which are usually written unqualified and
often cannot be qualified at all.

## Decision

Put a class's helper functions in the same namespace as the class.

- Declare free functions that form part of a type's interface — comparisons,
  conversions, named operations over the type — beside the type, in its
  namespace, and in the same header.
- Never declare an operator for a type outside that type's namespace, because
  argument-dependent lookup is the only mechanism that will reliably find it.
- Do not put a type's helpers into a namespace belonging to one of its callers,
  which makes the relationship invisible from the type's own header and hides
  the helper from argument-dependent lookup at every other call site.
- Where a helper is genuinely a caller's convenience rather than part of the
  type's interface, it belongs to the caller and this record does not apply.

## Rationale

Namespace membership is the only machine-readable statement that a free function
belongs to a type. Once
[0043](0043-c-4-make-a-function-a-member-only-if-it-needs-direct-access-to-the-representation-of-a-class.md)
has moved operations out of the class, they would otherwise be scattered with
nothing tying them back, and the type's interface would become impossible to
enumerate.

The operator argument is the decisive one, because it is about correctness
rather than tidiness. An `operator==` in the wrong namespace is not merely hard
to find — it is not found, and the program either fails to compile or, worse,
silently selects a different overload.

## Consequences

### Positive

- The full interface of a type is enumerable: it is the class plus the free
  functions beside it.
- Operators and other unqualified calls resolve by argument-dependent lookup.
- Callers do not have to name the namespace to use the type properly.

### Negative

- Argument-dependent lookup pulls the entire namespace into consideration for
  every call involving the type, so an unrelated overload in the same namespace
  can be selected unexpectedly. The larger the namespace, the worse this gets.
- It forbids adding helpers for a third-party type from outside, since the only
  conforming place is a namespace the helper's author does not own.
- Grouping by namespace conflicts with grouping by feature: a helper that serves
  two types in two namespaces has no correct home.

### Neutral

- The rule constrains where a helper is declared, not whether it should exist.
- Being in the namespace grants no access; a helper still works through the
  public interface.

## Enforcement

- Flag global functions that take argument types drawn from a single namespace
  while not being declared in that namespace themselves.

## Related

- [0043](0043-c-4-make-a-function-a-member-only-if-it-needs-direct-access-to-the-representation-of-a-class.md)
  — this record says where the functions that rule excludes from the class go.
- [0020](0020-i-1-make-interfaces-explicit.md) — a scattered helper set is an
  interface that is not stated anywhere.
