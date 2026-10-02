# 0265. CP.23: Think of a joining thread as a scoped container

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

To maintain pointer safety and avoid leaks, we need to consider what pointers
are used by a thread. If a thread joins, we can safely pass pointers to objects
in the scope of the thread and its enclosing scopes.

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
    joining_thread t0(f, &x);           // OK
    joining_thread t1(f, p);            // OK
    joining_thread t2(f, &glob);        // OK
    auto q = make_unique<int>(99);
    joining_thread t3(f, q.get());      // OK
    // ...
}
```

A `gsl::joining_thread` is a `std::thread` with a destructor that joins and that
cannot be detached. By "OK" we mean that the object will be in scope — live —
for as long as a thread can use the pointer to it. The fact that threads run
concurrently does not affect the lifetime or ownership issues here; these
threads can be seen as just a function object called from `some_fct`.

## Decision

Reason about a joining thread using the ordinary rules for a local object that
holds references.

- Where a thread joins at the end of its scope, treat pointers and references
  passed to it exactly as you would treat those captured by a local function
  object: anything alive for the enclosing scope is safe to pass.
- Declare such a thread with a type whose destructor joins — `std::jthread` in
  C++20, or `gsl::joining_thread` — rather than a bare `std::thread` plus a
  `join()` you have to remember.
- Do not let such a thread detach, because detaching is exactly what invalidates
  this reasoning.
- Make sure the thread object is declared after everything it points into, so
  that its join happens before those objects are destroyed.
- Where the thread must outlive the scope, do not reach for detach; move the
  thread object into a longer-lived scope, which changes which container it is.

## Rationale

The value of the rule is that it removes concurrency from the lifetime
argument. Once the thread is guaranteed to join before the scope ends, the
question "can this pointer dangle?" is the same question you would ask about a
callback stored in a local, and the answer comes from the ordinary scoping rules
rather than from reasoning about interleavings.

That is also why the joining must be structural. If the join depends on
reaching a statement, an early return or an exception breaks it, and the whole
analogy — and the lifetime guarantee that rests on it — disappears.

## Consequences

### Positive

- Lifetime questions for thread arguments reduce to ordinary scope questions.
- Passing a pointer to a local into a thread becomes a legitimate, checkable
  pattern rather than something to be avoided on principle.
- The existing enforcement for local object lifetimes applies without change.

### Negative

- Joining at the end of scope means the scope cannot exit until the thread
  finishes, so a slow or blocked thread stalls its creator, including during
  stack unwinding, where the wait is invisible in the source.
- The guarantee holds only if nothing detaches the thread, so the analogy rests
  on a separate rule being obeyed everywhere.
- Declaration order now matters for correctness in a way that is easy to break
  by moving a line.
- The guarantee needs a thread type whose destructor joins, which means either
  the GSL's `joining_thread` or C++20's `std::jthread`; neither is available to
  code that must build against an older standard without a dependency.

### Neutral

- The analogy is about lifetime and ownership only. It says nothing about data
  races on the objects pointed to, which the rule against data races governs.
- Passing a pointer to a static or to the free store remains safe here too; the
  rule widens what is safe rather than narrowing it.

## Enforcement

- Ensure that joining threads do not detach. After that, the usual lifetime and
  ownership enforcement for local objects applies.

## Related

- [0266](0266-cp-24-think-of-a-thread-as-a-global-container.md) — the same
  analysis for a thread that may detach, where much less is safe.
- [0267](0267-cp-25-prefer-gsl-joining-thread-over-std-thread.md) — the type
  that makes the join structural.
- [0268](0268-cp-26-dont-detach-a-thread.md) — the rule this one depends on.
- [0175](0175-es-5-keep-scopes-small.md) — the scope is what bounds the
  lifetime, so its size is part of the argument.
