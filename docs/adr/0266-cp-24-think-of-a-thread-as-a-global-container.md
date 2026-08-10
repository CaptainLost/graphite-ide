# 0266. CP.24: Think of a thread as a global container

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

To maintain pointer safety and avoid leaks, we need to consider what pointers
are used by a thread. If a thread is detached, we can safely pass pointers to
static and free store objects only.

```cpp
void f(int* p)
{
    // ...
    *p = 99;
    // ...
}
int glob = 33;
void some_fct(int* p)
{
    int x = 77;
    std::thread t0(f, &x);           // bad
    std::thread t1(f, p);            // bad
    std::thread t2(f, &glob);        // OK
    auto q = make_unique<int>(99);
    std::thread t3(f, q.get());      // bad
    // ...
    t0.detach();
    t1.detach();
    t2.detach();
    t3.detach();
    // ...
}
```

By "OK" we mean that the object will be in scope — live — for as long as the
thread can use the pointer to it. By "bad" we mean that the thread might use a
pointer after the pointed-to object is destroyed. The fact that threads run
concurrently does not affect the lifetime or ownership issues here; these
threads can be seen as just function objects called from `some_fct`.

Even objects with static storage duration can be problematic when used from
detached threads. If the thread continues until the end of the program, it might
be running concurrently with the destruction of objects with static storage
duration, so accesses to such objects might race.

This rule is redundant if you do not detach and use a joining thread type.
Converting code to follow those guidelines can be difficult and even impossible
for third-party libraries, and in such cases the rule becomes essential for
lifetime safety and type safety. In general it is undecidable whether a
`detach()` is executed for a thread, but simple common cases are easily
detected. If we cannot prove that a thread does not detach, we must assume that
it does and that it outlives the scope in which it was constructed; after that,
the usual lifetime and ownership enforcement for global objects applies.

## Decision

Where a thread may detach, assume it outlives every local scope, and pass it
only what has global lifetime.

- Pass a detachable thread pointers to objects with static storage duration, or
  to free store objects whose lifetime is guaranteed to exceed the thread's;
  pass nothing else by pointer or reference.
- Do not pass a pointer to a local, nor a pointer obtained from a locally owned
  `unique_ptr`, into a thread that might detach — including a pointer parameter
  received from a caller, whose own storage class you do not know.
- Where a detachable thread needs data, pass it by value, or hand it shared
  ownership.
- Treat a thread as detachable unless you can see that it cannot detach. A
  `std::thread` handed to code you cannot read, or whose disposal depends on a
  branch, counts as detachable.
- Remember that even a static object is not safe here: a thread running past
  the end of `main` may race with the destruction of static objects, so a
  detached thread should be stopped before that point.

## Rationale

The interesting part of this rule is the second half of the analogy, which the
first half tempts you to skip. Reasoning that a detached thread may only touch
globals gets the ownership question right and still leaves a race at shutdown,
because static objects have a lifetime that ends while the thread is running.

The other reason the rule survives is the undecidability. Because you cannot
prove in general that a thread never detaches, the safe default is to assume
that it does, which quietly means the strict version of this rule applies to
every bare `std::thread` you did not write.

## Consequences

### Positive

- Gives a usable rule for third-party code that cannot be converted to a joining
  thread type.
- The default — assume detachment unless proven otherwise — fails safe.
- Names the shutdown race explicitly, which is the part most analyses miss.

### Negative

- The permitted set is so small that useful work has to be done through globals
  or shared ownership, which pushes toward exactly the non-const global state
  other rules discourage.
- Assuming detachment for every unproven thread makes the rule pessimistic, and
  it will reject code that is in fact correct.
- Even the sanctioned option, a pointer to a static, is not actually safe at
  shutdown, so the rule's own permission carries an exception.
- It offers no way to know when a detached thread has finished, so the
  program cannot establish that its pointers have stopped being used.

### Neutral

- The rule is redundant for code that never detaches; it exists for code that
  might.
- Detecting `detach()` is undecidable in general but easy in the common cases,
  so enforcement is partial by construction.

## Enforcement

- Flag attempts to pass local variables to a thread that might detach.

## Related

- [0265](0265-cp-23-think-of-a-joining-thread-as-a-scoped-container.md) — the
  same analysis for a thread that joins, where much more is safe.
- [0268](0268-cp-26-dont-detach-a-thread.md) — the rule that makes this one
  unnecessary.
- [0270](0270-cp-32-to-share-ownership-between-unrelated-threads-use-shared-ptr.md)
  — how a detached thread can be given data that outlives its creator.
- [0021](0021-i-2-avoid-non-const-global-variables.md) — the state this rule
  pushes you toward is the state that record warns about.
