# 0262. CP.20: Use RAII, never plain lock()/unlock()

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Taking a mutex by hand and releasing it by hand invites nasty errors from
unreleased locks.

```cpp
mutex mtx;
void do_stuff()
{
    mtx.lock();
    // ... do stuff ...
    mtx.unlock();
}
```

Sooner or later someone will forget the `mtx.unlock()`, place a `return` in the
middle of the do-stuff section, throw an exception, or something similar. The
scoped-lock form has no such failure mode:

```cpp
mutex mtx;
void do_stuff()
{
    unique_lock<mutex> lck {mtx};
    // ... do stuff ...
}
```

## Decision

Acquire a mutex only by constructing a lock object, and release it only by
letting that object go out of scope.

- Use `std::scoped_lock`, `std::lock_guard` or `std::unique_lock` to take a
  mutex; do not call `lock()` or `unlock()` on a mutex directly.
- Give the lock object a name. An unnamed one is a temporary that is destroyed
  at the end of the full expression, so it locks and unlocks immediately.
- Use `unique_lock` where the lock must be handed to a condition variable or
  released before the end of the scope; use `lock_guard` or `scoped_lock`
  otherwise.
- To narrow a critical section, introduce a nested block around it rather than
  unlocking by hand in the middle of a function.
- Where an existing API forces a manual `lock()`/`unlock()` pair, wrap it in a
  small RAII handle at the boundary rather than spreading the pair through
  calling code.

## Rationale

The manual pair is a resource leak with a worse failure mode than most. A leaked
allocation costs memory; a leaked lock stops every other thread that ever wants
that mutex, and it does so at a moment unrelated to the code that caused it.

What makes the RAII form decisive is that it survives paths the author did not
write. An early `return` added a year later, or an exception thrown by something
three calls down, releases the lock without anyone revisiting the function.

## Consequences

### Positive

- The lock is released on every exit path, including the ones added later and
  the ones taken by exceptions.
- The extent of the critical section is visible as a scope rather than inferred
  by reading for the matching `unlock()`.
- It brings mutexes under the same resource discipline as memory and files,
  so there is one rule to learn rather than two.

### Negative

- Scope-shaped locking is less expressive than a manual pair; releasing early
  requires either `unique_lock::unlock` or an extra nested block, which makes
  the code more ceremonious than the thing it replaces.
- Introducing a block to narrow a critical section adds an indentation level
  whose purpose is not obvious to a reader who does not know this rule.
- The protection is only as good as the naming: an unnamed lock compiles,
  looks correct and locks nothing, which is a separate rule precisely because
  this one does not catch it.
- Some third-party and platform locking APIs offer no RAII wrapper, so one must
  be written and maintained.

### Neutral

- Which lock type to use is a separate question from whether to use one; this
  rule only settles the latter.
- The rule concerns how a mutex is taken, not which mutex or how long it is
  held; those are other records.

## Enforcement

- Flag calls of the member functions `lock()` and `unlock()`.

## Related

- [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
  — the general RAII rule; a mutex is a resource like any other.
- [0014](0014-p-8-do-not-leak-any-resources.md) — an unreleased lock is a leak.
- [0275](0275-cp-44-remember-to-name-your-lock-guards-and-unique-locks.md) — the
  hole this rule leaves, and how it is closed.
- [0274](0274-cp-43-minimize-time-spent-in-a-critical-section.md) — why the
  nested block, rather than a manual `unlock()`, is the way to shorten a
  critical section.
