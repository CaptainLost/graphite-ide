# 0120. C.149: Use unique_ptr or shared_ptr to avoid forgetting to delete objects created using new

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The reason is simply to avoid resource leaks. A raw pointer holding the result
of `new` places the obligation to `delete` on every path out of the scope,
including the ones nobody wrote down.

```cpp
void use(int i)
{
    auto p = new int {7};           // bad: initialize local pointers with new
    auto q = make_unique<int>(9);   // ok: guarantee the release of the memory-allocated for 9
    if (0 < i) return;              // maybe return and leak
    delete p;                       // too late
}
```

The `delete` is present, correct, and unreachable half the time. The early
return leaks; so would a thrown exception, and so would a `return` added later
by someone who never looked at the bottom of the function. `q` needs no such
attention: its destructor runs on every exit path, because that is what the
language guarantees for an object with automatic storage duration.

## Decision

Do not hold a newly created object in a raw pointer.

- Initialise a local owner as `std::unique_ptr` when the ownership is single,
  and `std::shared_ptr` only when ownership genuinely is shared.
- Construct through `std::make_unique` or `std::make_shared` rather than passing
  a raw `new` to a smart pointer constructor.
- Do not write `delete` on a local variable; if a `delete` is needed the object
  was owned by the wrong thing.
- Prefer an ordinary automatic object to any pointer when the object does not
  need to outlive the scope or be polymorphic.
- Pass a raw pointer or reference only as a non-owning observer, never to
  transfer ownership.

## Rationale

The leak in the example is not a missing `delete` — the `delete` is right there.
It is that the correctness of a hand-written release depends on the shape of the
whole function, and that shape changes over the life of the code. A destructor
is attached to the object rather than to a point in the control flow, so it
survives every later edit, including the exception paths that never appear in
the source at all.

## Consequences

### Positive

- Release happens on every exit path, including exceptional ones, without any
  path being enumerated.
- Ownership becomes visible in the type, so a reader can tell an owner from an
  observer without tracing the code.
- Adding an early return to an existing function stops being a way to introduce
  a leak.

### Negative

- `shared_ptr` is easy to reach for and expensive: atomic reference counts,
  a second allocation unless `make_shared` is used, and cycles that never
  release. The rule names it alongside `unique_ptr` without ranking them.
- Smart pointers appear in signatures and so spread through interfaces,
  coupling callers to the ownership scheme even where they only observe.
- A smart pointer does not fix ownership that is genuinely unclear; it only
  makes whichever answer was chosen automatic, including a wrong one.
- Some external and C APIs hand back raw owning pointers with bespoke
  deleters, so the boundary code still deals in raw `new`-like handles.

### Neutral

- The rule is about ownership of heap objects; it takes no position on whether
  the heap was needed.
- Which of the two smart pointers applies is decided by the ownership model, not
  by this record.

## Enforcement

- Flag initialisation of a naked pointer with the result of a `new`.
- Flag `delete` of a local variable.

## Related

- [0121](0121-c-150-use-make-unique-to-construct-objects-owned-by-unique-ptrs.md)
  and
  [0122](0122-c-151-use-make-shared-to-construct-objects-owned-by-shared-ptrs.md)
  — how the object should be constructed once it is owned by a smart pointer.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the principle this rule
  implements for memory.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the same distinction applied to interfaces.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — a smart pointer is the encapsulation of manual release.
