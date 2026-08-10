# 0211. ES.61: Delete arrays using delete[] and non-arrays using delete

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

That is what the language requires, and mistakes lead to resource release errors
or memory corruption.

```cpp
void f(int n)
{
    auto p = new X[n];   // n default constructed Xs
    // ...
    delete p;   // error: just delete the object p, rather than delete the array p[]
}
```

The type of `p` is `X*` in both cases, so nothing in the declaration records
which form of release is required. The compiler cannot diagnose the mismatch,
and the effect is undefined: typically the destructors of all but the first
element never run, and the deallocation is given the wrong size.

This example does not only violate the rule about naked `new`; it has several
other problems too. The right reading of this record is therefore that it
describes a mistake that should not be possible to make, rather than a
discipline to be maintained.

## Decision

Match the form of release to the form of allocation, and prefer not to be in a
position where the question arises.

- Use `delete[]` for anything allocated with `new T[n]`, and `delete` for
  anything allocated with `new T`.
- Do not allocate arrays with `new` at all. Use `std::vector` where the size can
  change, `std::array` where it is fixed at compile time, and
  `std::make_unique<T[]>(n)` where a bare owning buffer really is wanted; see
  [0210](0210-es-60-avoid-new-and-delete-outside-resource-management-functions.md).
- Do not store the result of an array `new` in a `std::unique_ptr<T>`; the
  array form is `std::unique_ptr<T[]>`, and the distinction has to be got right
  at the declaration rather than at the release.
- Never delete an array through a pointer to a base class; that is undefined
  regardless of which form is used. See
  [0123](0123-c-152-never-assign-a-pointer-to-an-array-of-derived-class-objects-to-a-pointer-to-its-base.md).

## Rationale

The interesting property of this rule is that following it correctly is not
enough. `delete[]` on an array is right, but the code containing it is still one
edit away from being wrong, because the fact that `p` points to an array lives
only in the reader's memory of a line further up.

That is the argument for the stronger form of the decision. The type
`std::unique_ptr<T[]>` carries the array-ness, so the release form is chosen
once, at the declaration, by the type system rather than by the author.

## Consequences

### Positive

- Removes a defect that corrupts the heap and typically manifests far from its
  cause.
- Choosing a container or the array form of `unique_ptr` moves the decision to
  the declaration, where it is visible and checkable.
- The destructors of every element actually run, which matters as soon as the
  element type owns anything.

### Negative

- The rule is only checkable in the narrow cases the upstream enforcement names
  — allocation and release in the same scope, or in a matching
  constructor/destructor pair. When the pointer crosses a function boundary,
  nothing can be verified.
- The type-level fix reproduces the same mistake one level up:
  `unique_ptr<T>` versus `unique_ptr<T[]>` is exactly as easy to get wrong as
  `delete` versus `delete[]`, and just as silent when wrong.
- `std::make_unique<T[]>` value-initialises, so replacing `new T[n]` with it is
  not always performance-neutral.
- The rule is subsumed by
  [0210](0210-es-60-avoid-new-and-delete-outside-resource-management-functions.md)
  in any code that follows that record, so it applies mainly to legacy and
  boundary code — which is also the code least likely to be reviewed against it.

### Neutral

- Whether a custom `operator new[]`/`operator delete[]` pair is involved does
  not change the rule; matched pairs are the subject of
  [0158](0158-r-15-always-overload-matched-allocation-deallocation-pairs.md).

## Enforcement

- If the `new` and the `delete` are in the same scope, mistakes can be flagged.
- If the `new` and the `delete` are in a constructor/destructor pair, mistakes
  can be flagged.

## Related

- [0210](0210-es-60-avoid-new-and-delete-outside-resource-management-functions.md)
  — the rule that makes this one unnecessary.
- [0158](0158-r-15-always-overload-matched-allocation-deallocation-pairs.md) —
  the same matching requirement for user-defined allocation.
- [0120](0120-c-149-use-unique-ptr-or-shared-ptr-to-avoid-forgetting-to-delete-objects-created-using-new.md)
  — the handle types that own the choice.
- [0123](0123-c-152-never-assign-a-pointer-to-an-array-of-derived-class-objects-to-a-pointer-to-its-base.md)
  — the related undefined case for arrays of derived objects.
