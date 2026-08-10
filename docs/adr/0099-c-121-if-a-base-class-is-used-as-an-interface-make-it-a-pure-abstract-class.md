# 0099. C.121: If a base class is used as an interface, make it a pure abstract class

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A class is more stable — less brittle — if it does not contain data. Interfaces
should normally be composed entirely of public pure virtual functions and a
default or empty virtual destructor.

```cpp
class My_interface {
public:
    // ...only pure virtual functions here ...
    virtual ~My_interface() {}   // or =default
};
```

```cpp
// bad
class Goof {
public:
    // ...only pure virtual functions here ...
    // no virtual destructor
};

class Derived : public Goof {
    string s;
    // ...
};

void use()
{
    unique_ptr<Goof> p {new Derived{"here we go"}};
    f(p.get()); // use Derived through the Goof interface
    g(p.get()); // use Derived through the Goof interface
} // leak
```

The `Derived` is deleted through its `Goof` interface, so its `string` is
leaked. Give `Goof` a virtual destructor and all is well.

The brittleness argument is about recompilation and about reach. Data in the
base fixes the layout of every derived class, so adding a member to the
interface changes the size and offsets of everything below it, and every
translation unit that saw the interface has to be rebuilt. It also makes the
interface partly an implementation: a derived class inherits state it did not
ask for and may not want.

## Decision

When a base class exists to be an interface, make it purely abstract.

- Declare every member function pure virtual and public, with no bodies in the
  base.
- Declare no data members, including no static ones with per-object meaning.
- Give the interface a public virtual destructor, `= default` unless there is a
  reason otherwise, or a protected non-virtual one where deletion through the
  interface is not intended.
- Do not put a constructor on the interface; there is nothing to construct.
- Where behaviour needs to be shared between implementations, put it in a
  separate implementation base or a free function, not in the interface.

## Rationale

The leak in the example is the argument that costs nothing to fix and everything
to miss. The compiler does not warn, the code is idiomatic, `unique_ptr` looks
like it is taking care of things, and the destructor of the actual object is
never called — the one line of interface boilerplate that prevents it is the
virtual destructor.

The data-free requirement is a separate and more structural claim: an interface
with no data has no layout to break, so it can be extended and reordered
without an ABI break for anyone deriving from it in another translation unit.
That is what "stable" means here.

## Consequences

### Positive

- Deleting through the interface destroys the whole object, so no derived state
  leaks.
- A data-free interface can change without relayout, so derived classes and
  users are insulated.
- Each implementation owns all of its own state, with no inherited members it
  did not choose.

### Negative

- Every implementation must supply every function, so behaviour common to all of
  them is duplicated or has to be factored into a second base — more types for
  the same design.
- A virtual destructor and vtable pointer make the type non-trivially
  destructible and non-trivially copyable, which rules out some optimisations
  and some `constexpr` use.
- Every call through the interface is an indirect one; where the set of
  implementations is fixed and known, a variant or a template would be cheaper.

### Neutral

- The protected non-virtual destructor is a real alternative, not a
  second-class one, when deletion through the base is deliberately not offered.
- Whether an interface should exist at all is the subject of
  [0098](0098-c-120-use-class-hierarchies-to-represent-concepts-with-inherent-hierarchical-structure-only.md);
  this record only says what one should look like.

## Enforcement

- Warn on any class that contains data members and also has an overridable
  (non-`final`) virtual function that was not inherited from a base class.

## Related

- [0102](0102-c-127-a-class-with-a-virtual-function-should-have-a-virtual-or-protected-destructor.md)
  — the destructor half of this rule, stated on its own.
- [0100](0100-c-122-use-abstract-classes-as-interfaces-when-complete-separation-of-interface-and-implementation-is-needed.md)
  — when the separation is worth its cost.
- [0104](0104-c-129-when-designing-a-class-hierarchy-distinguish-between-implementation-inheritance-and-interface-inheritance.md)
  — why data in a base makes a hierarchy brittle.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — the same preference stated as an interface rule.
