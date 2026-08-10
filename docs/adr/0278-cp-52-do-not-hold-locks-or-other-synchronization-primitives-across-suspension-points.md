# 0278. CP.52: Do not hold locks or other synchronization primitives across suspension points

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

This pattern creates a significant risk of deadlock. Some types of wait allow
the current thread to perform additional work until the asynchronous operation
has completed; if the thread holding the lock performs work that requires the
same lock, it will deadlock, because it is trying to acquire a lock it already
holds.

If the coroutine completes on a different thread from the one that acquired the
lock, that is undefined behaviour. Even with an explicit return to the original
thread, an exception might be thrown before the coroutine resumes, with the
result that the lock guard is not destroyed.

```cpp
std::mutex g_lock;
std::future<void> Class::do_something()
{
    std::lock_guard<std::mutex> guard(g_lock);
    co_await something(); // DANGER: coroutine has suspended execution while holding a lock
    co_await somethingElse();
}
```

The fix is to close the critical section before suspending:

```cpp
std::mutex g_lock;
std::future<void> Class::do_something()
{
    {
        std::lock_guard<std::mutex> guard(g_lock);
        // modify data protected by lock
    }
    co_await something(); // OK: lock has been released before coroutine suspends
    co_await somethingElse();
}
```

The pattern is also bad for performance. When a suspension point such as
`co_await` is reached, execution of the current function stops and other code
begins to run. It may be a long time before the coroutine resumes, and for that
entire duration the lock is held and cannot be acquired by other threads to do
work.

## Decision

Release every lock before the coroutine suspends.

- Put each critical section inside a nested block that ends before any
  `co_await` or `co_yield`, so the lock object is destroyed before the
  suspension.
- Never let a `lock_guard`, `unique_lock`, `scoped_lock` or other
  synchronisation object be live across a suspension point.
- Do the same for any primitive whose release is tied to a thread — a
  recursive mutex's count and anything with thread affinity — because the
  coroutine may resume on a different thread.
- Where state must be consistent across a suspension, read it under the lock,
  release, suspend, then re-acquire and re-validate. Do not assume it is
  unchanged.
- Treat this as a rule about placement of the block, not about the choice of
  lock type; no lock type makes holding one across a suspension safe.

## Rationale

Three separate failures share this one cause, and eliminating the pattern
eliminates all of them. The deadlock is the visible one; the second is that a
mutex is owned by a thread, so resuming on a different thread means the unlock
comes from a thread that does not own the lock, which is undefined behaviour;
the third is that the guard may never be destroyed at all if an exception
intervenes before resumption.

The performance argument is the one that applies even when the code is correct.
A suspension has no bound on how long it lasts, so a lock held across one is a
critical section of unbounded duration — the exact opposite of what the rule
about short critical sections asks for.

## Consequences

### Positive

- Removes deadlock, the cross-thread unlock, and the unreleased guard together.
- Bounds every critical section to code that does not suspend, so its duration
  is knowable.
- The remedy is a nested block, which costs nothing and is checkable.

### Negative

- It makes some operations genuinely harder to express: an operation that needs
  a consistent view across an asynchronous call cannot have one, and must be
  rewritten to re-read and re-validate.
- Re-acquiring after suspension introduces a window in which other threads can
  change the state, so the rule converts a deadlock into a correctness problem
  that must be handled explicitly.
- Splitting a coroutine into locked and suspending parts adds nesting to code
  that is already harder to follow than a plain function.
- It does not extend to whatever the awaited operation itself locks; a coroutine
  can still deadlock through the awaitable.

### Neutral

- The rule applies to any synchronisation primitive, not only to mutexes.
- It is a special case of keeping critical sections short, distinguished by the
  fact that here the excess duration is unbounded rather than merely long.

## Enforcement

- Flag all lock guards that are not destroyed before a coroutine suspends.

## Related

- [0274](0274-cp-43-minimize-time-spent-in-a-critical-section.md) — the general
  rule; a suspension point is the extreme case.
- [0264](0264-cp-22-never-call-unknown-code-while-holding-a-lock-e-g-a-callback.md)
  — the same hazard when the unbounded code is a callback rather than a
  continuation.
- [0262](0262-cp-20-use-raii-never-plain-lock-unlock.md) — the guard whose
  destruction is what the nested block is for.
- [0277](0277-cp-51-do-not-use-capturing-lambdas-that-are-coroutines.md) — the
  other thing that must not be assumed to survive a suspension.
