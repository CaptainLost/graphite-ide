# 0058. C.35: A base class destructor should be either public and virtual, or protected and non-virtual

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

To prevent undefined behaviour. If the destructor is public, then calling code
can attempt to destroy a derived class object through a base class pointer, and
the result is undefined if the base class's destructor is non-virtual. If the
destructor is protected, then calling code cannot destroy through a base class
pointer and the destructor does not need to be virtual; it does need to be
protected rather than private, so that derived destructors can invoke it. In
general, the writer of a base class does not know the appropriate action to be
taken upon destruction.

```cpp
struct Base {  // BAD: implicitly has a public non-virtual destructor
    virtual void f();
};

struct D : Base {
    string s {"a resource needing cleanup"};
    ~D() { /* ... do some cleanup ... */ }
    // ...
};

void use()
{
    unique_ptr<Base> p = make_unique<D>();
    // ...
} // p's destruction calls ~Base(), not ~D(), which leaks D::s and possibly more
```

A virtual function defines an interface to derived classes that can be used
without looking at those derived classes. If that interface allows destroying,
it should be safe to destroy.

A destructor must also be non-private, or it prevents using the type at all:

```cpp
class X {
    ~X();   // private destructor
    // ...
};

void use()
{
    X a;                        // error: cannot destroy
    auto p = make_unique<X>();  // error: cannot destroy
}
```

One case can be imagined for a protected virtual destructor: where an object of
a derived type, and only of such a type, should be allowed to destroy another
object — not itself — through a pointer to base. The guideline's authors report
never having seen such a case in practice.

## Decision

Give every base class a destructor in one of the two safe forms.

- If objects of the derived type may be destroyed through a pointer or reference
  to the base, make the base's destructor `public` and `virtual`, and default it
  where nothing else is needed.
- If they may not, make the base's destructor `protected` and non-virtual, which
  makes destruction through a base pointer a compile error and costs no vtable
  slot.
- Never leave a base class with an implicitly declared destructor; that gives a
  public non-virtual one, which is the unsafe combination.
- Never make a destructor private on a type that is meant to be instantiated.
- Declaring the destructor puts the class under the rule of five, so declare or
  `=delete` the copy and move operations at the same time, per
  [0052](0052-c-21-if-you-define-or-delete-any-copy-move-or-destructor-function-define-or-delete-them-all.md).

## Rationale

The unsafe combination is the one you get by writing nothing, which is why the
rule needs stating: a `struct Base` with a virtual function and no destructor
compiles cleanly, is used through `unique_ptr<Base>` without complaint, and
leaks or corrupts silently at every destruction.

The two permitted forms are not alternatives of equal standing but answers to a
design question — may a user destroy this through the base? Answering "yes"
costs a vtable entry and buys safe polymorphic deletion; answering "no" costs
nothing and makes the unsafe call impossible. What the rule forbids is not
answering.

## Consequences

### Positive

- Deleting a derived object through a base pointer is either correct or
  rejected, never undefined.
- The base class states whether polymorphic ownership is part of its contract.
- The protected form gives safety without the cost of virtual dispatch.

### Negative

- A public virtual destructor forces a vtable onto a class that might otherwise
  have had none, making objects larger and the type non-trivially destructible,
  which can disqualify it from optimisations elsewhere.
- The protected form makes the base unusable with `unique_ptr<Base>` and any
  other owning handle to the base, so the choice constrains the callers, not
  just the class.
- The rule applies to a class the moment it acquires a derived class, which can
  be in another translation unit or another project entirely; nothing in the
  base's own file signals that the rule now bites.

### Neutral

- A defaulted virtual destructor is the usual form for an abstract interface and
  needs no body.
- The imagined protected-virtual case is acknowledged upstream and has not been
  observed in practice.

## Enforcement

- A class with any virtual functions should have a destructor that is either
  public and virtual, or protected and non-virtual.
- If a class inherits publicly from a base class, that base class should have a
  destructor that is either public and virtual, or protected and non-virtual.

## Related

- [0052](0052-c-21-if-you-define-or-delete-any-copy-move-or-destructor-function-define-or-delete-them-all.md)
  — declaring the destructor drags in the other four.
- [0048](0048-c-10-prefer-concrete-types-over-class-hierarchies.md) — the cost
  described here is one reason to prefer a concrete type.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — the interface classes most often subject to this rule.
- [0060](0060-c-37-make-destructors-noexcept.md) — the other property every
  destructor should be given deliberately.
