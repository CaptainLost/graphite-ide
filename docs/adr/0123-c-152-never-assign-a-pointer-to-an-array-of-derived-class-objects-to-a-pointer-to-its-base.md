# 0123. C.152: Never assign a pointer to an array of derived class objects to a pointer to its base

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Subscripting the resulting base pointer will lead to invalid object access and
probably to memory corruption.

```cpp
struct B { int x; };
struct D : B { int y; };
void use(B*);

D a[] = { {1, 2}, {3, 4}, {5, 6} };
B* p = a;     // bad: a decays to &a[0] which is converted to a B*
p[1].x = 7;   // overwrite a[0].y
use(a);       // bad: a decays to &a[0] which is converted to a B*
```

Pointer arithmetic advances by the size of the pointee type, and the pointee
type here is `B`. The array's elements are `D`s, spaced `sizeof(D)` apart, so
`p[1]` addresses a position that is not the start of any element. Writing
through it corrupts the tail of the first element. Nothing about this requires a
cast: the array name decays to a pointer to its first element, and a pointer to
a derived class converts implicitly to a pointer to its base, so two conversions
the language grants freely compose into an expression whose every subsequent use
is undefined.

The same happens on the call to `use`, and there the corruption occurs in
another function, written by somebody with no reason to suspect the pointer they
were handed does not point into an array of `B`.

## Decision

Never let an array of derived objects be converted to a pointer to the base.

- Do not initialise or assign a `Base*` from an array of `Derived`.
- Do not pass an array of `Derived` to a parameter of type `Base*`.
- Pass an array as a `span` rather than as a pointer, and do not let the array
  name suffer a derived-to-base conversion before it gets into the span:
  `span<Derived>` is correct and `span<Base>` will not compile from it.
- Where a sequence of objects must be handled polymorphically, hold pointers or
  smart pointers to them, not the objects themselves, since a sequence of
  pointers is uniformly sized whatever the objects are.

## Rationale

This is the one place where two separately reasonable implicit conversions
combine into undefined behaviour with no cast, no warning, and no syntax to look
at. The array decay discards the count, the derived-to-base conversion discards
the element size, and what remains is a pointer that is well-formed, non-null,
and wrong for every index but zero. Refusing the conversion at the point of the
array is the only cheap moment to catch it; after that the wrongness is spread
across arbitrary code that has no way to detect it.

## Consequences

### Positive

- Indexing corruption of this kind becomes impossible rather than unlikely.
- Passing ranges as `span<Derived>` keeps both the element type and the extent,
  so neither of the two discarded facts is discarded.
- The error surfaces at the point where the types are known, not inside a callee
  that cannot tell.

### Negative

- The rule forbids something the language explicitly permits, so it is a
  convention with no compiler support unless a checker is configured; the
  offending line compiles cleanly.
- Handling a sequence polymorphically now requires an array of pointers, which
  costs an indirection per element and usually an allocation per object,
  destroying the contiguity the array was chosen for.
- C interfaces that take `Base*` plus a count cannot express this constraint at
  all, so boundary code must convert element by element.

### Neutral

- The rule is about arrays; a single `Derived*` converting to `Base*` is fine
  and is the normal way to use a hierarchy.
- Nothing here depends on the classes being polymorphic — the example has no
  virtual functions — only on the derived type being larger.

## Enforcement

- Flag all combinations of array decay and derived-to-base conversion.
- Pass an array as a `span` rather than as a pointer, and do not let the array
  name suffer a derived-to-base conversion before getting into the span.

## Related

- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the interface
  rule with the same `draw(Shape*, int)` failure, and the `span` remedy.
- [0116](0116-c-145-access-polymorphic-objects-through-pointers-and-references.md)
  — slicing is the single-object form of this size mismatch.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — a
  breach of type safety reached entirely through implicit conversions.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) —
  `span<Derived>` turns the error into a compile failure.
