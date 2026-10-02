# 0350. T.81: Do not mix hierarchies and arrays

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

An array of derived class objects can implicitly decay to a pointer to a base
class, with potentially disastrous results. Assume that `Apple` and `Pear` are
two kinds of `Fruit`:

```cpp
void maul(Fruit* p)
{
    *p = Pear{};     // put a Pear into *p
    p[1] = Pear{};   // put a Pear into p[1]
}

Apple aa[] = { an_apple, another_apple };   // aa contains Apples (obviously!)
maul(aa);
Apple& a0 = aa[0];   // a Pear?
Apple& a1 = aa[1];   // a Pear?
```

`aa[0]` probably now holds a `Pear`, without a cast having been written
anywhere. Worse, if `sizeof(Apple) != sizeof(Pear)` then the access to `aa[1]`
is not aligned to the start of an object in the array at all. This is a type
violation and probably a memory corruption. Never write such code.

`maul()` also violates the rule that a `T*` points to an individual object, not
to a sequence.

Using a proper container removes the decay:

```cpp
void maul2(Fruit* p)
{
    *p = Pear{};   // put a Pear into *p
}

vector<Apple> va = { an_apple, another_apple };   // va contains Apples (obviously!)
maul2(va);       // error: cannot convert a vector<Apple> to a Fruit*
maul2(&va[0]);   // you asked for it
```

The container blocks the accidental conversion; it does not stop somebody
taking the address of an element and handing it over deliberately. The
assignment inside `maul2()` still slices.

## Decision

Never let an array of derived objects be treated as a sequence of base objects.

- Do not pass an array of a derived type where a pointer to a base type is
  expected; there is no signature under which that is meaningful.
- Store polymorphic objects in a container of pointers or smart pointers —
  `vector<unique_ptr<Fruit>>` — rather than a container of base objects.
- Take a `span<T>` or a container reference rather than a `T*` when a function
  operates on a sequence, so the element type is part of the signature.
- Do not assign through a pointer or reference to a base; use a virtual
  operation instead.

## Rationale

Two separate language rules combine here, and each is harmless alone: arrays
decay to pointers, and a derived pointer converts to a base pointer. Together
they let a caller hand a function an array whose element size the function does
not know, and the resulting pointer arithmetic addresses the middle of an
object. Nothing in the type system objects, no cast appears in the source, and
the corruption surfaces far from `maul`.

## Consequences

### Positive

- The two mechanisms that produce the corruption — array decay and
  derived-to-base conversion — stop meeting.
- Element size travels with the type, so indexing is computed correctly.
- Polymorphic storage becomes explicit: a container of pointers says that the
  elements vary in type.

### Negative

- A container of pointers costs an indirection per element and an allocation
  per object, which is a real price for code that is not actually polymorphic.
- The protection is only against the implicit path. `maul2(&va[0])` still
  compiles, and taking the address of an element is a normal thing to do.
- Slicing through the base reference is a separate hazard that this rule does
  not address; suppressing copy and assignment on the polymorphic base is what
  closes that.
- The rule offers no help at a C ABI boundary, where a pointer and a count is
  the only vocabulary available.

### Neutral

- Arrays of a concrete type with no derived classes are unaffected.
- The upstream example is written to be alarming rather than realistic; the
  same shape occurs quietly whenever a legacy `T*` interface is fed a modern
  container's data.

## Enforcement

- (Hard) Detect the conversion of an array of derived objects to a pointer to a
  base.

## Related

- [0123](0123-c-152-never-assign-a-pointer-to-an-array-of-derived-class-objects-to-a-pointer-to-its-base.md)
  — the same defect stated as a rule about the assignment itself.
- [0148](0148-r-2-in-interfaces-use-raw-pointers-to-denote-individual-objects-only.md)
  — `maul(Fruit*)` is already wrong before any array is passed to it.
- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the interface
  rule that would have prevented the call from compiling.
