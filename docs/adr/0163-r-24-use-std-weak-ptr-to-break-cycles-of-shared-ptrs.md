# 0163. R.24: Use std::weak_ptr to break cycles of shared_ptrs

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

`shared_ptr`s rely on use counting, and the use count for a cyclic structure
never goes to zero, so a mechanism is needed to destroy a cyclic structure at
all.

```cpp
#include <memory>
class bar;
class foo {
public:
  explicit foo(const std::shared_ptr<bar>& forward_reference)
    : forward_reference_(forward_reference)
  { }
private:
  std::shared_ptr<bar> forward_reference_;
};
class bar {
public:
  explicit bar(const std::weak_ptr<foo>& back_reference)
    : back_reference_(back_reference)
  { }
  void do_something()
  {
    if (auto shared_back_reference = back_reference_.lock()) {
      // Use *shared_back_reference
    }
  }
private:
  std::weak_ptr<foo> back_reference_;
};
```

The forward edge owns; the back edge observes. `bar` gets at its `foo` by
calling `lock()`, which yields a `shared_ptr` that is empty if the `foo` is gone
and otherwise keeps it alive for the duration of the use. Without the `weak_ptr`
the two objects would keep each other's counts at one forever, and neither
destructor would ever run.

The guidelines carry an unresolved argument about how to describe this. One view
is that `weak_ptr` is better characterised as "temporary shared ownership" than
as "breaking cycles"; the reply is that breaking cycles is what you must do, and
temporarily sharing ownership is how you do it — you could temporarily share
ownership just as well with another `shared_ptr`, and that would not break
anything.

## Decision

Where shared ownership can form a cycle, make one direction weak.

- Decide which edge of the cycle is ownership and which is observation, and hold
  the observing edge as a `std::weak_ptr`.
- Access a `weak_ptr` only through `lock()`, and handle the empty result; do not
  test `expired()` and then lock, because the answer can change in between.
- Hold the `shared_ptr` returned by `lock()` for as long as the object is used,
  and no longer.
- Parent-to-child edges own; child-to-parent, observer, cache and registry edges
  observe.
- Prefer not to have the cycle. Exclusive ownership with borrowed raw pointers,
  or an arena addressed by index or handle, has neither the cycle nor the
  refcount.

## Rationale

Reference counting is the one part of `shared_ptr` that does not degrade
gracefully: a cycle does not leak slowly or intermittently, it never releases,
and no test will report it because every individual operation was correct.
`weak_ptr` is the only tool in the language that participates in the counting
scheme without contributing to it, so making one edge weak is not a style
preference but the only available fix short of redesigning the ownership graph.

## Consequences

### Positive

- Cyclic structures can be destroyed at all, which is otherwise impossible under
  reference counting.
- The lifetime relationship is stated in the member's type: one side owns, the
  other watches.
- `lock()` makes the "it might be gone" case explicit at every use, rather than
  leaving a raw back-pointer to dangle.

### Negative

- Which direction is weak is a design decision the type system will not make for
  you, and getting it backwards produces the same permanent retention with none
  of the symptoms visible in review.
- Every access costs a `lock()`, an atomic operation, and a branch for the
  expired case, which makes back-edge traversal materially more expensive than a
  pointer.
- A `weak_ptr` keeps the control block alive after the object is destroyed, and
  under `make_shared` that keeps the object's storage alive too.
- The failure is undetectable by tools: as the guidelines note, if cycles could
  be found statically there would be no need for `weak_ptr` in the first place.

### Neutral

- `weak_ptr` requires `shared_ptr`, so adopting it means the object is
  shared-owned; this rule does not argue that it should be.
- Whether `weak_ptr` is best described as breaking cycles or as temporary shared
  ownership is an open argument upstream; the mechanism is the same either way.

## Enforcement

- No practical automatic check. If cycles could be detected statically, there
  would be no need for `weak_ptr`.

## Related

- [0160](0160-r-21-prefer-unique-ptr-over-shared-ptr-unless-you-need-to-share-ownership.md)
  — the cheapest way to avoid this problem is not to share.
- [0159](0159-r-20-use-unique-ptr-or-shared-ptr-to-represent-ownership.md) — the
  ownership types this rule repairs.
- [0171](0171-r-37-do-not-pass-a-pointer-or-reference-obtained-from-an-aliased-smart-pointer.md)
  — `lock()` producing a local owner is the same "pin it first" move.
- [0161](0161-r-22-use-make-shared-to-make-shared-ptrs.md) — the layout that
  makes a surviving `weak_ptr` retain the object's storage.
