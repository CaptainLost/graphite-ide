# 0086. C.84: A swap function must not fail

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

`swap` is widely used in ways that are assumed never to fail, and programs
cannot easily be written to work correctly in the presence of a failing `swap`.
The standard-library containers and algorithms will not work correctly if a swap
of an element type fails.

```cpp
void swap(My_vector& x, My_vector& y)
{
    auto tmp = x;   // copy elements
    x = y;
    y = tmp;
}
```

This is not just slow: if a memory allocation occurs for the elements in `tmp`,
this swap could throw, and would make standard algorithms fail if used with such
elements.

## Decision

Implement `swap` so that it cannot fail, and give the type the members that make
that possible.

- Exchange the existing representations — swap pointers, sizes, and other
  members in place. Do not copy, do not allocate, and do not call anything
  fallible.
- Never implement `swap` as copy-to-temporary, assign, assign back.
- Where a member has its own non-failing `swap`, call it; where it is a
  built-in, use `std::swap`.
- If a type cannot be swapped without a failing step, do not give it a `swap` at
  all rather than giving it one that can fail.
- Mark the result `noexcept`, per
  [0087](0087-c-85-make-swap-noexcept.md), so callers and the standard library
  can rely on it.

## Rationale

Every user of `swap` is a user that has no recovery path. The strong-guarantee
idiom uses `swap` as the commit step precisely because it is the point past
which nothing can go wrong; a failing swap leaves both objects in an
indeterminate half-exchanged state with no way to restore either, so the caller
is not merely unable to handle the failure, there is nothing left to handle it
with.

The bad example is also the slow one, which is the usual pattern: the
implementation that allocates is both the one that can fail and the one that
defeats the reason for calling `swap` in the first place.

## Consequences

### Positive

- The strong-guarantee idioms built on `swap` actually hold.
- Standard containers and algorithms behave correctly with the type as an
  element.
- The non-failing implementation is also the fast one, so there is no trade to
  make.

### Negative

- The requirement propagates to every member: a type containing a member whose
  swap can allocate cannot satisfy this rule, and the fix is to change the
  member's type, which is a much larger change than the swap.
- "Must not fail" is not checkable — a `swap` that calls something fallible
  compiles fine, and under `noexcept` the failure becomes a call to
  `std::terminate` rather than a reported error.
- It rules out generic swap implementations that go through serialisation, deep
  copy, or any allocating exchange, even where those would be the natural way to
  write the operation for the type in question.

### Neutral

- The requirement is not a preference for one error mechanism over another: a
  `swap` returns nothing and its callers are usually generic code mid-operation,
  so there is no channel through which a failure could be reported at all.
- The rule sits alongside the same requirement for destructors and deallocation
  functions, which fail for the same reason.

## Enforcement

- (Simple) When a class has a `swap` member function, it should be declared
  `noexcept`.

## Related

- [0085](0085-c-83-for-value-like-types-consider-providing-a-noexcept-swap-function.md)
  — how to write the `swap` this record constrains.
- [0087](0087-c-85-make-swap-noexcept.md) — the annotation that states the
  requirement to the compiler.
- [0079](0079-c-65-make-move-assignment-safe-for-self-assignment.md) — `swap` is
  built from moves, so its correctness rests on theirs.
