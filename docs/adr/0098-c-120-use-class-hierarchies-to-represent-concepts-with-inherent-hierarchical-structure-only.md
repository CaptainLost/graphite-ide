# 0098. C.120: Use class hierarchies to represent concepts with inherent hierarchical structure (only)

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Direct representation of ideas in code eases comprehension and maintenance. Make
sure the idea represented in the base class exactly matches all derived types,
and that there is not a better way to express it than using the tight coupling
of inheritance.

Do not use inheritance when simply having a data member will do. Usually,
needing inheritance means that the derived type needs to override a base virtual
function or needs access to a protected member.

```cpp
class DrawableUIElement {
public:
    virtual void render() const = 0;
    // ...
};

class AbstractButton : public DrawableUIElement {
public:
    virtual void onClick() = 0;
    // ...
};

class PushButton : public AbstractButton {
    void render() const override;
    void onClick() override;
    // ...
};

class Checkbox : public AbstractButton {
// ...
};
```

Do not represent non-hierarchical domain concepts as class hierarchies:

```cpp
// bad
template<typename T>
class Container {
public:
    // list operations:
    virtual T& get() = 0;
    virtual void put(T&) = 0;
    virtual void insert(Position) = 0;
    // ...
    // vector operations:
    virtual T& operator[](int) = 0;
    virtual void sort() = 0;
    // ...
    // tree operations:
    virtual void balance() = 0;
    // ...
};
```

Here most overriding classes cannot implement most of the functions required in
the interface well, so the base class becomes an implementation burden.
Furthermore, the user of `Container` cannot rely on the member functions
actually performing meaningful operations reasonably efficiently; a call might
throw an exception instead. Users therefore have to resort to run-time checking,
or to abandoning this over-general interface in favour of a particular interface
found by a run-time type inquiry such as a `dynamic_cast`.

## Decision

Use a class hierarchy only where the concepts being modelled are genuinely
hierarchical.

- Before deriving, check that every operation on the base is meaningful and
  implementable for every intended derived class. If most derived classes would
  implement some operation by throwing or by doing nothing, the base is wrong.
- Derive only when the derived type needs to override a virtual function or to
  reach a protected member. If neither is true, hold the would-be base as a data
  member instead.
- Do not build a base class by taking the union of the operations of its
  intended derived classes; take the intersection, and split the hierarchy where
  the intersection is empty.
- Where a set of types shares behaviour but not a common concept, express the
  commonality with a template or a concept rather than a base class.

## Rationale

The sharpest test in the rule is behavioural rather than conceptual: a base
class whose derived classes implement most of its functions by throwing has
already failed, whatever the domain model says, because it has pushed every
caller into run-time checking. That is a symptom anyone can look for.

The second half of the rule — do not inherit where a member will do — is worth
as much as the first. Inheritance is the tightest coupling C++ offers: it fixes
layout, exposes protected state, and makes the base part of the derived class's
public identity. Composition costs one member and one forwarding call, and
almost nothing else.

## Consequences

### Positive

- Interfaces stay implementable, so callers can rely on every operation they
  see.
- The need for `dynamic_cast` to recover a usable interface largely disappears.
- Rejecting inheritance-for-reuse keeps types loosely coupled and independently
  changeable.

### Negative

- Composition costs forwarding: every operation the wrapper exposes has to be
  written out, where inheritance would have given it for free.
- Splitting an over-general base into several narrower ones multiplies the
  number of types, and callers that legitimately want more than one aspect now
  need multiple inheritance or a combined interface.
- "Inherent hierarchical structure" is a judgement, and reasonable people
  disagree about it until the hierarchy is already built and expensive to
  change.

### Neutral

- The rule is about whether to have a hierarchy, not about how to structure one
  once the answer is yes.
- Non-public inheritance of an empty class, of a template parameter, or of a
  class template specialised with the derived type is explicitly outside the
  enforcement below, because those are implementation techniques rather than
  concept modelling.

## Enforcement

- Look for classes with lots of members that do nothing but throw.
- Flag every use of a non-public base class `B` where the derived class `D` does
  not override a virtual function or access a protected member in `B`, and `B`
  is not one of the following: empty, a template parameter or parameter pack of
  `D`, or a class template specialised with `D`.

## Related

- [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md)
  — what the base should look like once a hierarchy is justified.
- [0104](0104-c-129-when-designing-a-class-hierarchy-distinguish-between-implementation-inheritance-and-interface-inheritance.md)
  — the two kinds of inheritance this rule is quietly separating.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — the interface-level statement of the same preference.
- [0007](0007-p-1-express-ideas-directly-in-code.md) — a hierarchy that does not
  match the domain expresses the wrong idea.
