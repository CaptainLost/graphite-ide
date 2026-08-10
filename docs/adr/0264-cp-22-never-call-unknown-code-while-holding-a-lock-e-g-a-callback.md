# 0264. CP.22: Never call unknown code while holding a lock (e.g., a callback)

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

If you do not know what a piece of code does, you are risking deadlock.

```cpp
void do_this(Foo* p)
{
    lock_guard<mutex> lck {my_mutex};
    // ... do something ...
    p->act(my_data);
    // ...
}
```

If you do not know what `Foo::act` does — maybe it is a virtual function
invoking a derived class member of a class not yet written — it might call
`do_this` recursively and cause a deadlock on `my_mutex`. Maybe it will lock a
different mutex and not return in a reasonable time, delaying every caller of
`do_this`.

A common form of the "calling unknown code" problem is a call to a function that
tries to gain locked access to the same object. That can often be solved with a
`recursive_mutex`:

```cpp
recursive_mutex my_mutex;
template<typename Action>
void do_something(Action f)
{
    unique_lock<recursive_mutex> lck {my_mutex};
    // ... do something ...
    f(this);    // f will do something to *this
    // ...
}
```

If, as is likely, `f()` invokes operations on `*this`, we must make sure that
the object's invariant holds before the call.

## Decision

Do not invoke code you do not control from inside a critical section.

- Do not call a virtual function, a function pointer, a `std::function`, a
  template parameter's operation, or any user-supplied callback while a lock is
  held.
- Restructure instead: take the lock, copy or move out what the callback needs,
  release the lock, then call.
- Where a notification must be delivered, collect the callbacks under the lock
  and dispatch them after the lock is released.
- Where re-entry is genuinely part of the design, use a `recursive_mutex`
  deliberately, and re-establish the object's invariant before making the call,
  because the callback may observe the object.
- Treat a call into another module as unknown code even when its current
  implementation is visible, since that implementation is free to change.

## Rationale

The problem is not the callback's behaviour today but the fact that the set of
things it may do is open. A virtual call inside a critical section makes every
present and future override a participant in this function's locking protocol,
without any of them being told.

Deadlock is only the loudest outcome. A callback that locks something else and
merely takes a long time turns one slow operation into contention on a mutex
that every other thread needs, and that failure shows up as a mysterious pause
rather than as a hang with a stack to inspect.

## Consequences

### Positive

- Removes self-deadlock through re-entry, which is the failure most likely to
  appear only in a rarely taken path.
- Bounds the time the lock is held by code you can read, so latency stops
  depending on strangers.
- Keeps the locking protocol private to the module that owns the mutex.

### Negative

- Copying state out so it can be used after unlocking costs a copy, and the
  copy can be stale by the time the callback sees it, which shifts a correctness
  problem rather than removing it.
- Deferring notifications until after the unlock changes their ordering
  relative to the state change, and callers sometimes depend on that ordering.
- "Unknown code" has no crisp boundary. Almost any non-trivial call can
  eventually reach a virtual function, so applied literally the rule forbids
  more than anyone follows.
- `recursive_mutex` makes the invariant obligation harder: the object must be
  consistent at every point where a callback might re-enter, not merely at the
  ends of the critical section.

### Neutral

- The rule is about what is called while a lock is held, not about whether
  callbacks are a good design.
- `recursive_mutex` is offered by the source as a remedy for one specific shape
  of the problem, not as a general licence.

## Enforcement

- Flag calling a virtual function with a non-recursive mutex held.
- Flag calling a callback with a non-recursive mutex held.

## Related

- [0263](0263-cp-21-use-std-lock-or-std-scoped-lock-to-acquire-multiple-mutexes.md)
  — the deadlock case that can be solved algorithmically, unlike this one.
- [0274](0274-cp-43-minimize-time-spent-in-a-critical-section.md) — a callback
  is the least bounded thing that can be inside one.
- [0278](0278-cp-52-do-not-hold-locks-or-other-synchronization-primitives-across-suspension-points.md)
  — the same hazard when the unknown code is whatever runs after a suspension.
- [0262](0262-cp-20-use-raii-never-plain-lock-unlock.md) — the scope discipline
  that makes "inside the critical section" a visible region.
