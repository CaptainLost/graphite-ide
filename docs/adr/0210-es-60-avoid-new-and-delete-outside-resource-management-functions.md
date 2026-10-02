# 0210. ES.60: Avoid new and delete outside resource management functions

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Direct resource management in application code is error-prone and tedious. This
is also known as the rule of "No naked new!".

```cpp
void f(int n)
{
    auto p = new X[n];   // n default constructed Xs
    // ...
    delete[] p;
}
```

There can be code in the elided part that causes the `delete` never to happen: a
return, a break, a `throw`, or simply a later edit that adds one. The
allocation and the release are two statements separated by an arbitrary amount
of code, and nothing in the language relates them.

The rule does not forbid `new` and `delete` outright; it confines them to the
functions whose job is resource management — the constructors and destructors of
handle types, and factory functions that hand the result straight to a handle.
Everywhere else, the handle is used and the raw operations are not written.

## Decision

Do not write `new` or `delete` in ordinary code.

- Allocate through `std::make_unique` or `std::make_shared`, or through a
  container, rather than writing `new`; see
  [0121](0121-c-150-use-make-unique-to-construct-objects-owned-by-unique-ptrs.md)
  and
  [0122](0122-c-151-use-make-shared-to-construct-objects-owned-by-shared-ptrs.md).
- Prefer a scoped object to any heap allocation at all where the lifetime allows
  it; see [0151](0151-r-5-prefer-scoped-objects-dont-heap-allocate-unnecessarily.md).
- Where `new` is unavoidable — a custom deleter, an unusual allocation strategy,
  a C API that hands back a raw pointer — write it inside a resource management
  type or a factory function that returns an owning handle, and never let the
  raw pointer escape that function.
- Do not write a bare `delete` at all. If a `delete` is needed, it belongs in a
  destructor, per
  [0055](0055-c-31-all-resources-acquired-by-a-class-must-be-released-by-the-classs-destructor.md).
- Do not use a raw array where a `std::vector` or `std::array` would do.

## Rationale

The reason the paired `new`/`delete` in the example is wrong is not that
somebody might forget the `delete` — it is right there — but that its execution
is conditional on everything in between, and that condition is invisible and
grows over time. Every early return added to that function later is a leak, and
the person adding it has no reason to look for one.

A handle inverts the dependency: release is attached to the object rather than
to a path through the code, so correctness stops depending on the shape of the
control flow.

## Consequences

### Positive

- Leaks stop being a function of control flow, which means an added early return
  or an unexpected exception cannot introduce one.
- The `new`/`delete` mismatch category — including the array form covered by
  [0211](0211-es-61-delete-arrays-using-delete-and-non-arrays-using-delete.md) —
  disappears from ordinary code.
- The remaining `new` calls are few and localised, so they can be reviewed
  properly rather than skimmed.

### Negative

- The rule relocates the problem rather than removing it. Resource management
  types still contain the raw operations, and those types now have to be written
  correctly, tested, and given the right copy and move semantics per
  [0052](0052-c-21-if-you-define-or-delete-any-copy-move-or-destructor-function-define-or-delete-them-all.md).
- `std::make_unique` cannot express a custom deleter, and neither `make_unique`
  nor `make_shared` supports placement new, so any pool, arena or
  aligned-allocation strategy needs its own wrapper written by hand.
- `std::make_unique<T[]>(n)` value-initialises the elements, which costs where a
  bare `new T[n]` would not. Getting the performance back requires
  `std::make_unique_for_overwrite` or a container, which is more to know.
- C libraries return raw pointers with matching free functions, so the boundary
  code holds a custom-deleter `unique_ptr` for every such resource, which is
  more ceremony than the `delete` it replaces for a short-lived local.

### Neutral

- The rule is about where the operations appear, not about whether the program
  uses the heap. A program with a great deal of dynamic allocation can satisfy
  it completely.

## Enforcement

Flag naked `new`s and naked `delete`s.

## Related

- [0154](0154-r-11-avoid-calling-new-and-delete-explicitly.md) — the same rule
  stated in the resource management section.
- [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
  — the mechanism that makes this possible.
- [0211](0211-es-61-delete-arrays-using-delete-and-non-arrays-using-delete.md) —
  the mistake this rule makes unexpressible.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the requirement this serves.
