# 0160. R.21: Prefer unique_ptr over shared_ptr unless you need to share ownership

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

A `unique_ptr` is conceptually simpler and more predictable — you know when
destruction happens — and faster, because you do not implicitly maintain a use
count.

This needlessly adds and maintains a reference count:

```cpp
void f()
{
    shared_ptr<Base> base = make_shared<Derived>();
    // use base locally, without copying it -- refcount never exceeds 1
} // destroy base
```

This is more efficient:

```cpp
void f()
{
    unique_ptr<Base> base = make_unique<Derived>();
    // use base locally
} // destroy base
```

The example is deliberately unglamorous. Nothing is shared, nothing escapes the
function, and the count never exceeds one; the `shared_ptr` version pays for a
control block, an atomic increment and an atomic decrement in exchange for
nothing at all. That is the common case, not an unusual one — `shared_ptr` is
frequently chosen because the ownership question was never asked.

## Decision

Make `unique_ptr` the default owning pointer.

- Use `shared_ptr` only when there are genuinely several owners and no one of
  them determines the object's lifetime.
- "Several things point at it" is not shared ownership. Observers should hold a
  raw pointer or a reference, borrowed from the single owner for the duration of
  the use.
- A local owning pointer that is never copied out of the function is a
  `unique_ptr`.
- Converting a `unique_ptr` to a `shared_ptr` later is a move; plan for the
  simpler type first and promote only when a second owner actually appears.

## Rationale

The two types differ in what they let you avoid thinking about, and that is the
real cost. `unique_ptr` forces the ownership graph to be decided, and having
decided it you can say when each object dies. `shared_ptr` lets the decision be
deferred indefinitely, and the price is that no one can any longer say where a
given object is destroyed, which turns destructor side effects and reference
cycles into runtime discoveries. The atomic operations are the smaller half of
the argument.

## Consequences

### Positive

- Destruction happens at a knowable point, so destructor side effects are
  reviewable.
- No control block, no atomic refcount traffic, and the pointer stays the size
  of a raw pointer.
- The ownership graph has to be articulated, which usually improves the design
  that produced the question.

### Negative

- Deciding sole ownership up front is real design work, and getting it wrong
  means rewriting the ownership plumbing rather than adding a copy.
- Anything that needs to observe an object safely across time — a cache, an
  observer list, an async continuation — needs `weak_ptr`, which requires
  `shared_ptr`, so those designs are pushed toward sharing whatever this rule
  says.
- A `unique_ptr` owner means every borrower depends on the owner outliving it,
  and nothing checks that; the dangling raw pointer replaces the refcount cost
  with a lifetime obligation.
- The rule is stated as a preference, so it does not resolve the case where
  ownership is arguably shared, which is exactly where the decision is hard.

### Neutral

- Both are covered by the previous rule; this one only orders them.
- `unique_ptr` converts to `shared_ptr` but not back, so the default here is
  also the more reversible choice.

## Enforcement

- (Simple) Warn if a function uses a shared pointer for an object allocated
  within the function but never returns it and never passes it to a function
  requiring a shared pointer reference. Suggest `unique_ptr` instead.

## Related

- [0159](0159-r-20-use-unique-ptr-or-shared-ptr-to-represent-ownership.md) — the
  rule that introduces both.
- [0163](0163-r-24-use-std-weak-ptr-to-break-cycles-of-shared-ptrs.md) — the
  cycle problem that only `shared_ptr` has.
- [0166](0166-r-32-take-a-unique-ptr-widget-parameter-to-express-that-a-function-assumes-ownership-of-a-widget.md)
  — how exclusive ownership moves across a call.
- [0168](0168-r-34-take-a-shared-ptr-widget-parameter-to-express-shared-ownership.md)
  — what taking a `shared_ptr` in an interface commits callers to.
