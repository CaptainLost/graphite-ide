# 0122. C.151: Use make_shared() to construct objects owned by shared_ptrs

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Section C states this rule as a cross-reference: the substance lives in the
resource-management rule R.22, "Use `make_shared()` to make `shared_ptr`s", and
is transcribed here.

`make_shared` gives a more concise statement of the construction. It also gives
an opportunity to eliminate a separate allocation for the reference counts, by
placing the `shared_ptr`'s use counts next to its object.

```cpp
shared_ptr<X> p1 { new X{2} }; // bad
auto p = make_shared<X>(2);    // good
```

The `make_shared()` version mentions `X` only once, so it is usually shorter, as
well as faster, than the version with the explicit `new`. Constructing a
`shared_ptr` from a raw `new` requires two allocations — one for the object and
one for the control block holding the strong and weak counts. That form does
not leak on its own: the `shared_ptr` constructor is required to delete the
pointer it was given if it cannot allocate the control block, and since C++17 a
call's arguments are sequenced whole against one another rather than
interleaved. What it does is write the allocation and the acquisition of
ownership as two steps, which a later edit can pull apart and which a reader
has to check.

## Decision

Construct an object that a `shared_ptr` will own with `std::make_shared`.

- Write `auto p = std::make_shared<T>(args...)` rather than
  `std::shared_ptr<T> p{new T{args...}}`.
- Never pass a bare `new` expression as an argument to a `shared_ptr`
  constructor or to any other function.
- Use the `shared_ptr` constructor directly where `make_shared` cannot express
  the construction: a custom deleter, an aliasing constructor, a pointer from a
  foreign allocator, or a type whose constructor `make_shared` cannot reach.
- Do not choose `shared_ptr` merely because `make_shared` is convenient; single
  ownership through
  [0121](0121-c-150-use-make-unique-to-construct-objects-owned-by-unique-ptrs.md)
  remains the default.

## Rationale

For `unique_ptr` the argument is concision and exception safety; for
`shared_ptr` there is a third, which is that the control block and the object
can share one allocation only if the library is allowed to make them together.
That is not a micro-optimisation the caller can recover later — writing
`shared_ptr<X>{new X}` forecloses it at the point of construction, permanently,
for every copy of that pointer.

## Consequences

### Positive

- One allocation instead of two, and the counts sit next to the object rather
  than a cache miss away.
- The type is written once, so the owner and the object cannot drift apart.
- Allocation and acquisition of ownership are one call, so they cannot be
  separated by a later edit.

### Negative

- The single allocation keeps the object's storage alive as long as any
  `weak_ptr` survives, even after the object is destroyed, so a large object
  observed by a long-lived `weak_ptr` holds its whole footprint in memory. The
  two-allocation form does not have this problem.
- `make_shared` cannot take a custom deleter, so resources with bespoke release
  are excluded from the rule.
- It value-initialises, which is unwanted work for large trivially constructible
  buffers; avoiding it means reaching for C++20's
  `std::make_shared_for_overwrite` instead.
- Following this rule makes shared ownership pleasant to write, which is a
  hazard of its own: `shared_ptr` still costs atomic counting and still permits
  reference cycles that never release.

### Neutral

- `make_shared` for arrays requires C++20; earlier standards provide no array
  form of it.
- This rule concerns construction only; whether ownership should be shared at
  all is decided elsewhere.

## Enforcement

- (Simple) Warn if a `shared_ptr` is constructed from the result of `new` rather
  than `make_shared`.

## Related

- [0121](0121-c-150-use-make-unique-to-construct-objects-owned-by-unique-ptrs.md)
  — the same rule for unique ownership.
- [0120](0120-c-149-use-unique-ptr-or-shared-ptr-to-avoid-forgetting-to-delete-objects-created-using-new.md)
  — the rule that puts the object in a smart pointer to begin with.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the eliminated second
  allocation, and the weak-pointer cost that pulls the other way.
- [0014](0014-p-8-do-not-leak-any-resources.md) — keeping the allocation from
  ever existing unowned is the other half of the reason.
