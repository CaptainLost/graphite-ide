# 0116. C.145: Access polymorphic objects through pointers and references

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

If you have a class with a virtual function, you do not in general know which
class provided the function that ought to be used. A polymorphic object handled
by value is copied into whatever the destination type is, and everything the
destination type does not know about is discarded.

```cpp
struct B { int a; virtual int f(); virtual ~B() = default; };
struct D : B { int b; int f() override; };

void use(B b)
{
    D d;
    B b2 = d;   // slice
    B b3 = b;
}

void use2()
{
    D d;
    use(d);   // slice
}
```

Both `d`s are sliced. The copy is a well-formed `B`, so nothing complains; it
simply is not the object that was written down, and calls on it dispatch to
`B::f` rather than `D::f`.

The exception is narrow: a named polymorphic object can be accessed safely in
the scope of its own definition, where its dynamic type is not in doubt. Just
do not slice it.

```cpp
void use3()
{
    D d;
    d.f();   // OK
}
```

The complementary guideline is that a polymorphic class should suppress
copying, so that slicing cannot be written in the first place.

## Decision

Handle any object of polymorphic type through a pointer or a reference, not by
value.

- Take a parameter of polymorphic type as `const B&`, `B&`, or a pointer; never
  as `B`.
- Return a polymorphic object through a reference or an owning smart pointer,
  never by value in the base type.
- Do not store a polymorphic object in a container of the base type by value;
  store a pointer or a smart pointer.
- Suppress copying on a polymorphic class — delete or protect the copy
  operations — so a slice cannot be written at all.
- Use a named object of a concrete derived type directly, without a handle, only
  in the scope where it is defined and where its type is therefore known.

## Rationale

Slicing is the one type error in C++ that has no syntactic tell. There is no
cast, no conversion the reader can see, no diagnostic — an assignment or a
by-value argument that reads as ordinary copying quietly produces a different
object of a different type with different behaviour. Handling polymorphic
objects only through indirection removes the operation that does this, rather
than asking each author to remember not to write it.

## Consequences

### Positive

- Virtual dispatch reaches the function the object actually provides, because
  the object is never replaced by a truncated copy.
- Interfaces stated in terms of `const B&` compose with every derived class,
  present and future, without change.
- Deleting the copy operations on a polymorphic base turns the whole class of
  slicing bugs into compile errors.

### Negative

- Indirection introduces lifetime questions that value semantics did not have:
  a reference or pointer to a polymorphic object can dangle, and the rule pushes
  every polymorphic object into that category.
- Copying a polymorphic object becomes an explicit operation, usually a virtual
  `clone`, which is more code than `=` and one more thing an implementer of a
  derived class must remember to write.
- Polymorphic types can no longer be stored directly in a `vector`, so
  ownership, indirection and cache behaviour all become the caller's problem.
- The permitted exception — a named object accessed in its own scope — is stated
  as a property of the programmer's knowledge rather than something a tool can
  confirm.

### Neutral

- The rule concerns access, not ownership; which smart pointer, if any, holds
  the object is a separate decision.
- A class with no virtual functions is unaffected; value semantics remain the
  default for concrete types.

## Enforcement

- Flag all slicing.

## Related

- [0117](0117-c-146-use-dynamic-cast-where-class-hierarchy-navigation-is-unavoidable.md)
  — once the object is behind a pointer or reference, this is how to ask what it
  is.
- [0123](0123-c-152-never-assign-a-pointer-to-an-array-of-derived-class-objects-to-a-pointer-to-its-base.md)
  — the array form of the same base-versus-derived size mismatch.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — slicing
  is a silent breach of type safety.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — an abstract interface cannot be sliced, because it cannot be instantiated.
