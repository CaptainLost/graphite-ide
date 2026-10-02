# 0274. CP.43: Minimize time spent in a critical section

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

The less time is spent with a mutex taken, the less chance there is that another
thread has to wait — and thread suspension and resumption are expensive.

```cpp
void do_something() // bad
{
    unique_lock<mutex> lck(my_lock);
    do0();  // preparation: does not need lock
    do1();  // transaction: needs locking
    do2();  // cleanup: does not need locking
}
```

Here we are holding the lock for longer than necessary: we should not have taken
it before we needed it, and should have released it again before starting the
cleanup. We could rewrite this as:

```cpp
void do_something() // bad
{
    do0();  // preparation: does not need lock
    my_lock.lock();
    do1();  // transaction: needs locking
    my_lock.unlock();
    do2();  // cleanup: does not need locking
}
```

But that compromises safety and violates the rule to use RAII. Instead, add a
block for the critical section:

```cpp
void do_something() // OK
{
    do0();  // preparation: does not need lock
    {
        unique_lock<mutex> lck(my_lock);
        do1();  // transaction: needs locking
    }
    do2();  // cleanup: does not need locking
}
```

## Decision

Hold a lock for the transaction and nothing else.

- Do the preparation before taking the lock and the cleanup after releasing it.
- Narrow a critical section by wrapping it in a nested block, so the lock object
  is still an RAII object with a scope, rather than by calling `lock()` and
  `unlock()` by hand.
- Do not perform input or output, allocate large amounts of memory, or wait on
  anything while a lock is held.
- Do not call unknown code, such as a virtual function or a callback, from
  inside the critical section; its duration is not yours to bound.
- Take a lock at the point of use rather than at the top of a function, and do
  not extend a critical section merely to avoid an extra pair of braces.

## Rationale

Both halves of the reason matter and they are different. Contention is the
obvious one: a longer critical section means a higher chance another thread
arrives while it is held. The second is that the penalty for losing that race is
not proportional to the overrun — a thread that has to wait is suspended and
later resumed, which costs far more than the work it was denied.

The rule also decides between two ways of narrowing a section, and the choice is
not free. The manual `lock()`/`unlock()` pair is shorter and it is rejected,
because a critical section that is short and occasionally never exits is worse
than one that is longer and always does.

## Consequences

### Positive

- Reduces both the frequency of contention and the number of suspensions it
  causes.
- The nested block makes the protected region visible as a scope rather than
  inferred from a pair of calls.
- Preparation and cleanup work runs concurrently with other threads instead of
  serialising behind the mutex.

### Negative

- Splitting a function into locked and unlocked parts adds nesting and can make
  a short function harder to read than the version that simply locked
  throughout.
- Narrowing a critical section can be a correctness change, not just a
  performance one: state read before the lock is taken may be stale by the time
  the transaction runs, and moving work out is only safe if it truly does not
  touch the guarded data.
- A shorter critical section usually means more of them, and taking and
  releasing a mutex repeatedly has its own cost.
- The rule pulls against taking all needed mutexes at once, which asks you to
  acquire everything up front and therefore to hold some of them longer.

### Neutral

- The rule is about how long a lock is held, not about which mutex is taken or
  how many.
- Introducing a block for this purpose is the intended idiom, and the extra
  nesting is accepted rather than regretted.

## Enforcement

- Impossible in general. Flag naked `lock()` and `unlock()` calls.

## Related

- [0262](0262-cp-20-use-raii-never-plain-lock-unlock.md) — the rule that rejects
  the manual pair this record's second example uses.
- [0264](0264-cp-22-never-call-unknown-code-while-holding-a-lock-e-g-a-callback.md)
  — the least bounded thing that can appear in a critical section.
- [0263](0263-cp-21-use-std-lock-or-std-scoped-lock-to-acquire-multiple-mutexes.md)
  — the rule that pulls the other way.
- [0271](0271-cp-40-minimize-context-switching.md) — the suspension and
  resumption cost cited here.
