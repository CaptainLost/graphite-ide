# 0036. I.25: Prefer empty abstract classes as interfaces to class hierarchies

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Abstract classes that are empty — that have no non-static member data — are more
likely to be stable than base classes with state.

```cpp
class Shape {  // bad: interface class loaded with data
public:
    Point center() const { return c; }
    virtual void draw() const;
    virtual void rotate(int);
    // ...
private:
    Point c;
    vector<Point> outline;
    Color col;
};
```

This forces every derived class to compute a centre, even if that is non-trivial
and the centre is never used. Similarly, not every `Shape` has a `Color`, and
many shapes are best represented without an outline defined as a sequence of
`Point`s. An abstract class is better:

```cpp
class Shape {    // better: Shape is a pure interface
public:
    virtual Point center() const = 0;   // pure virtual functions
    virtual void draw() const = 0;
    virtual void rotate(int) = 0;
    // ...
    // ... no data members ...
    // ...
    virtual ~Shape() = default;
};
```

## Decision

Prefer an empty abstract class as an interface to a base class carrying state.

- A class used as an interface has no non-static data members, and its member
  functions are pure virtual.
- It declares a virtual destructor, defaulted, so that derived objects can be
  destroyed through it.
- Representation decisions stay in the derived classes, where the type that
  actually has the data can choose how to store it.
- Where shared implementation is genuinely wanted, factor it into a separate
  class that implementations use, rather than into the interface they inherit.

## Rationale

State in a base class is a decision imposed on every implementation that will
ever exist, including the ones nobody has thought of yet. The `Shape` example is
the standard demonstration: a centre stored in the base is wrong for shapes
whose centre is computed, and an outline is wrong for shapes that have none.

An empty interface is also stable in the ABI sense — no data layout to change —
and in the design sense: adding a data member later affects every derived class,
whereas an interface with no data has nothing to change except its function set.

## Consequences

### Positive

- Implementations choose their own representation instead of inheriting one.
- The interface is more stable, in layout and in design, over time.
- Multiple interfaces can be implemented by one class without the layout
  problems that stateful bases create.

### Negative

- Common implementation shared by all derived classes has to be written again,
  or delegated to, rather than inherited — real duplication that a stateful base
  would have avoided.
- Every call through the interface is virtual, with the indirection that
  implies, where a stored member would have been a direct read.
- More types: an interface plus its implementations, where a single class would
  have done for a hierarchy that never grew.

### Neutral

- This record concerns classes used as interfaces, not class hierarchies in
  general; implementation inheritance among concrete classes is a separate
  subject.
- Whether to use runtime polymorphism at all, versus templates or variants, is
  not decided here.

## Enforcement

- (Simple) Warn if a pointer or reference to a class `C` is assigned to a
  pointer or reference to a base of `C`, and the base class contains data
  members.

## Related

- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — the
  other way of keeping representation out of an interface.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — the compile-time counterpart to an abstract interface.
