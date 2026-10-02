# 0083. C.81: Use =delete when you want to disable default behavior (without wanting an alternative)

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

In a few cases, a default operation is not desirable. `= delete` removes it and
turns every use into a compile error naming the deleted function:

```cpp
class Immortal {
public:
    ~Immortal() = delete;   // do not allow destruction
    // ...
};

void use()
{
    Immortal ugh;   // error: ugh cannot be destroyed
    Immortal* p = new Immortal{};
    delete p;       // error: cannot destroy *p
}
```

A `unique_ptr` can be moved but not copied. To achieve that, its copy operations
are deleted; to prevent copying it is necessary to `=delete` the copy operations
from lvalues:

```cpp
template<class T, class D = default_delete<T>> class unique_ptr {
public:
    // ...
    constexpr unique_ptr() noexcept;
    explicit unique_ptr(pointer p) noexcept;
    // ...
    unique_ptr(unique_ptr&& u) noexcept;   // move constructor
    // ...
    unique_ptr(const unique_ptr&) = delete; // disable copy from lvalue
    // ...
};

unique_ptr<int> make();   // make "something" and return it by moving

void f()
{
    unique_ptr<int> pi {};
    auto pi2 {pi};      // error: no move constructor from lvalue
    auto pi3 {make()};  // OK, move: the result of make() is an rvalue
}
```

Deleted functions should be public, so that the diagnostic says the function is
deleted rather than that it is inaccessible.

## Decision

Remove an unwanted operation with `= delete`, publicly, and only where no
alternative is intended.

- Write `= delete` on the copy or move operations of a type that must not be
  copied or moved, rather than declaring them private and leaving them
  undefined.
- Keep deleted functions `public`, so the compiler reports "deleted" rather than
  "private".
- Use `= delete` on the operation itself when there is nothing the caller should
  do instead. Where there is an alternative — a `clone()`, a named `assign` —
  the alternative is the point, and the deletion is only what closes the wrong
  door.
- Do not delete an operation to work around an implementation difficulty; the
  deletion should follow from the type's semantics.

## Rationale

`= delete` differs from the older private-and-undefined idiom in where the error
appears: a deleted function fails at overload resolution, in the caller, with
the function named, while a private undefined one fails at link time in a member
or friend and not at all elsewhere. The diagnostic is the whole value of the
feature.

Keeping the deletion public is what makes that diagnostic legible, which is why
the guideline bothers to say it: access checking happens after overload
resolution, so a private deleted function reports the wrong problem.

## Consequences

### Positive

- The unwanted use fails at the call site, at compile time, with the deleted
  function named.
- The type's declaration states what it cannot do, rather than leaving it to be
  inferred from a missing definition.
- Deleting a specific overload can reject an unwanted conversion without
  rejecting the function.

### Negative

- Deletion is total and permanent for the type: there is no way for a friend, a
  derived class, or the type's own implementation to reach the operation, so a
  deletion chosen for interface reasons also constrains the implementation.
- Deleting one special member has knock-on effects on the others under the
  language's implicit-declaration rules, so a single `= delete` can quietly
  remove operations nobody meant to remove.
- The elimination of a default operation cannot be checked mechanically — it
  depends on the intended semantics of the class — so tooling can only flag it
  for human review.

### Neutral

- Its counterpart, for operations that should exist with default behaviour, is
  [0082](0082-c-80-use-default-if-you-have-to-be-explicit-about-using-the-default-semantics.md).
- The `Immortal` example is illustrative rather than advice; a type that cannot
  be destroyed is rarely what anyone wants.

## Enforcement

- The elimination of a default operation should be based on the desired
  semantics of the class. Consider such classes suspect, but maintain a positive
  list of classes where a human has asserted that the semantics is correct.

## Related

- [0082](0082-c-80-use-default-if-you-have-to-be-explicit-about-using-the-default-semantics.md)
  — the other half of being explicit about the special members.
- [0081](0081-c-67-a-polymorphic-class-should-suppress-public-copy-move.md) — the
  main occasion for using `= delete` in a hierarchy.
- [0075](0075-c-61-a-copy-operation-should-copy.md) — a type that cannot copy
  meaningfully should delete the copy rather than define a partial one.
- [0020](0020-i-1-make-interfaces-explicit.md) — a deleted function is part of
  the interface, stating what is not offered.
