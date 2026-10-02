# 0048. C.10: Prefer concrete types over class hierarchies

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A concrete type is fundamentally simpler than a type in a class hierarchy:
easier to design, easier to implement, easier to use, easier to reason about,
smaller, and faster. A hierarchy needs a reason — actual use cases — not merely
the possibility that one might appear.

```cpp
class Point1 {
    int x, y;
    // ... operations ...
    // ... no virtual functions ...
};

class Point2 {
    int x, y;
    // ... operations, some virtual ...
    virtual ~Point2();
};

void use()
{
    Point1 p11 {1, 2};   // make an object on the stack
    Point1 p12 {p11};    // a copy

    auto p21 = make_unique<Point2>(1, 2);   // make an object on the free store
    auto p22 = p21->clone();                // make a copy
    // ...
}
```

If a class is part of a hierarchy, its objects must in real code be manipulated
through pointers or references. That implies more memory overhead, more
allocations and deallocations, and more run-time overhead to perform the
resulting indirections. Copying stops being a copy and becomes a virtual
`clone`. Concrete types, by contrast, can be stack-allocated and can be members
of other classes.

Indirection is fundamental to run-time polymorphic interfaces; the allocation
and deallocation overhead is not, it is merely the most common case. A base
class can be used as the interface of a scoped object of a derived class, which
is done where dynamic allocation is prohibited, such as hard real-time work, and
to provide a stable interface to some kinds of plug-in.

## Decision

Make a type concrete unless a use case requires run-time polymorphism.

- Start with a value type: no virtual functions, no virtual destructor,
  copyable, storable by value in containers and as a member of other types.
- Introduce a hierarchy only when there is an existing need to hold objects of
  more than one type behind one interface and dispatch between them at run time.
  A future possibility is not such a need.
- Where an interface is required, prefer the narrow form of
  [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — an abstract class with no state — rather than a base class carrying data.
- Where polymorphism is needed but allocation is not, hold a derived object as a
  scoped object and pass a reference to its base.
- Do not add a virtual destructor "just in case"; it commits the type to the
  hierarchy, per
  [0058](0058-c-35-a-base-class-destructor-should-be-either-public-and-virtual-or-protected-and-non-virtual.md).

## Rationale

The cost of a hierarchy is not the virtual call, it is that the type stops being
a value. Once objects must be reached through pointers, lifetime becomes a
question the compiler no longer answers, copying becomes a member function
someone has to write correctly, and every container of the type becomes a
container of allocations.

The guideline's separation of indirection from allocation is the useful nuance:
the argument against hierarchies is largely an argument against the allocation
that usually accompanies them, and a scoped derived object behind a base
reference keeps the polymorphism while paying almost none of the cost.

## Consequences

### Positive

- Objects live on the stack or inside their owners, so lifetime is decided by
  scope rather than by discipline.
- Copying, comparison and assignment mean what they say, which is what
  [0049](0049-c-11-make-concrete-types-regular.md) asks for.
- Fewer allocations, no indirection on member access, and a smaller object.

### Negative

- A type that turns out to need polymorphism later is expensive to convert:
  every by-value use, every container, and every member declaration is affected.
  The rule trades a known cost now for a possible larger cost later.
- Avoiding a hierarchy where variation genuinely exists pushes the variation
  into `switch` statements over a tag, or into `std::variant`, which is not
  obviously simpler and is harder to extend.
- "You need a reason" is not a testable criterion, so the rule is applied by
  judgement and argued about case by case.

### Neutral

- The rule does not forbid hierarchies; it sets the default and asks for a
  justification.
- Where a hierarchy is chosen, the remaining C.hier and C.ctor rules govern how
  it is built.

## Related

- [0049](0049-c-11-make-concrete-types-regular.md) — what a concrete type should
  then be.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — the form a hierarchy should take when one is justified.
- [0058](0058-c-35-a-base-class-destructor-should-be-either-public-and-virtual-or-protected-and-non-virtual.md)
  — the obligation a type takes on the moment it becomes a base.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the allocation and
  indirection this record avoids.
