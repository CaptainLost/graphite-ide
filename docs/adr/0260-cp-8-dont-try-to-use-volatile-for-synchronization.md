# 0260. CP.8: Don't try to use volatile for synchronization

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

In C++, unlike some other languages, `volatile` does not provide atomicity, does
not synchronise between threads, and does not prevent instruction reordering by
either the compiler or the hardware. It simply has nothing to do with
concurrency.

```cpp
int free_slots = max_slots; // current source of memory for objects
Pool* use()
{
    if (int n = free_slots--) return &pool[n];
}
```

This is perfectly good code in a single-threaded program, but let two threads
execute it and there is a race condition on `free_slots`, so two threads might
get the same value. That is a bad data race, and people trained in other
languages might try to fix it like this:

```cpp
volatile int free_slots = max_slots; // current source of memory for objects
Pool* use()
{
    if (int n = free_slots--) return &pool[n];
}
```

This has no effect on synchronisation: the data race is still there. The C++
mechanism for this is the atomic types:

```cpp
atomic<int> free_slots = max_slots; // current source of memory for objects
Pool* use()
{
    if (int n = free_slots--) return &pool[n];
}
```

Now the `--` operation is atomic, rather than a read-increment-write sequence
where another thread might get in between the individual operations.

The alternative the guidelines offer is direct: use atomic types where you might
have used `volatile` in another language, and use a mutex for anything more
complicated. `volatile` retains its own, unrelated purpose — telling the
compiler that an object may be changed by something outside the program, such as
memory-mapped hardware — and that purpose is the subject of a separate rule.

## Decision

Never reach for `volatile` to make an access thread-safe.

- Use `std::atomic<T>` where a single object is read and written by more than
  one thread and the operations on it are simple.
- Use a mutex, held through a scoped lock, for anything involving more than one
  object or more than one step.
- Do not add `volatile` to a variable in response to a race, a stale read or a
  debugger observation; it changes nothing about the race.
- Reserve `volatile` for its actual purpose: an object whose value can change
  outside the program's control, such as a hardware register.
- Do not treat a `volatile` qualifier already present in code as evidence that
  the access was thought about.

## Rationale

The two hazards this rule addresses do not look alike but have the same cure.
Losing an update because a read-modify-write was interleaved is one; the
compiler assuming no other thread writes the object, and optimising on that
assumption, is another. `volatile` addresses neither, and `atomic` addresses
both.

What makes the mistake worth a rule of its own is that `volatile` is the right
answer in other languages and looks like a fix here. The code after the change
is still undefined behaviour, but now carries a marker suggesting somebody
considered concurrency, which makes the defect harder to find than it was
before.

## Consequences

### Positive

- Directs a common wrong reflex to the mechanism that actually works.
- `atomic<int>` reads at the call site exactly like the `int` it replaces, so
  the correct version is no harder to write than the broken one.
- Removes a misleading marker that suppresses review.

### Negative

- Atomics are not free: they constrain the compiler and cost more than a plain
  load or store on contended data, so replacing every shared `int` with an
  `atomic` has a measurable price.
- The rule says what not to use, but choosing between an atomic and a mutex
  still needs the judgement the rule does not supply.
- Making individual variables atomic does not make an operation spanning several
  of them atomic, and this rule can encourage exactly that mistake.
- `volatile` remains legal and remains necessary for its own purpose, so it
  cannot simply be banned outright.

### Neutral

- The upstream rule has no enforcement clause, so this record has none.
- Nothing is asserted here about memory ordering beyond what the source states;
  the default ordering of the standard atomic operations is deliberately not
  discussed.

## Related

- [0257](0257-cp-2-avoid-data-races.md) — the race that `volatile` fails to
  fix, and what a race actually costs.
- [0276](0276-cp-50-define-a-mutex-together-with-the-data-it-guards-use-synchronized-value-t-where-possible.md)
  — the mutex route, for the more complicated cases this rule defers.
- [0262](0262-cp-20-use-raii-never-plain-lock-unlock.md) — how that mutex is
  taken.
- [0021](0021-i-2-avoid-non-const-global-variables.md) — `free_slots` is a
  mutable global, which is where the problem started.
