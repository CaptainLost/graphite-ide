# 0161. R.22: Use make_shared() to make shared_ptrs

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

`make_shared` gives a more concise statement of the construction. It also gives
an opportunity to eliminate a separate allocation for the reference counts, by
placing the `shared_ptr`'s use counts next to its object.

```cpp
shared_ptr<X> p1 { new X{2} }; // bad
auto p = make_shared<X>(2);    // good
```

The `make_shared()` version mentions `X` only once, so it is usually shorter, as
well as faster, than the version with the explicit `new`.

There is a third property the upstream reason does not spell out: `make_shared`
performs the allocation and the ownership handoff in a single call, so there is
no point at which an allocated object exists without an owner. That is what
makes it the recommended fix for the multiple-allocations-per-statement problem.

## Decision

Create a `shared_ptr` with `std::make_shared`.

- Write `auto p = std::make_shared<X>(args...)` rather than `std::shared_ptr<X>
  p{new X{args...}}`.
- Use `std::allocate_shared` when a specific allocator is required.
- Construct a `shared_ptr` from a raw pointer only where `make_shared` cannot
  express what is needed: a custom deleter, an object allocated elsewhere, or
  adoption of a handle from a C API. Do so in a statement of its own.
- Where the object may be large and long-lived weak references are expected,
  consider the two-allocation form deliberately rather than by accident.

## Rationale

`make_shared` is shorter, allocates once instead of twice, gives better locality
between the object and its control block, and closes the window between
allocation and ownership. Those are four separate arguments pointing the same
way, and the only thing traded away is a storage-lifetime subtlety that matters
in a minority of designs. A rule with that shape should be the default, and the
exceptions written down where they are taken.

## Consequences

### Positive

- One allocation rather than two, with the control block adjacent to the object.
- The type is named once, so the declaration cannot disagree with the
  allocation.
- There is no moment at which the object is allocated but unowned.

### Negative

- The single allocation means the object's storage is not released until the
  last `weak_ptr` expires, not merely the last `shared_ptr`. For a large object
  observed by long-lived weak references, that is a real retention cost, and the
  fix is to go back to the form this rule discourages.
- `make_shared` cannot take a custom deleter, so every resource that is not
  plain heap memory is excluded from the rule.
- It cannot construct through a private constructor without a friend declaration
  or a passkey, which pushes factory designs toward the raw form.
- It value-initialises; `make_shared_for_overwrite`, added in C++20, is needed
  to avoid that for large buffers, and is easy to overlook.

### Neutral

- Whether a `shared_ptr` is the right owner at all is the previous rule's
  question, not this one's.
- The same argument applies to `make_unique`, minus the control block.

## Enforcement

- (Simple) Warn if a `shared_ptr` is constructed from the result of `new` rather
  than by `make_shared`.

## Related

- [0162](0162-r-23-use-make-unique-to-make-unique-ptrs.md) — the same rule for
  exclusive ownership.
- [0156](0156-r-13-perform-at-most-one-explicit-resource-allocation-in-a-single-expression-statement.md)
  — the problem `make_shared` is named as the best solution to.
- [0159](0159-r-20-use-unique-ptr-or-shared-ptr-to-represent-ownership.md) — why
  a `shared_ptr` is being made in the first place.
- [0163](0163-r-24-use-std-weak-ptr-to-break-cycles-of-shared-ptrs.md) — the
  weak references that interact with the single-allocation layout.
