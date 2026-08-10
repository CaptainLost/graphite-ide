# 0267. CP.25: Prefer gsl::joining_thread over std::thread

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

A joining thread is a thread that joins at the end of its scope. Detached
threads are hard to monitor, and it is harder to ensure the absence of errors in
detached — and potentially detached — threads.

```cpp
void f() { std::cout << "Hello "; }
struct F {
    void operator()() const { std::cout << "parallel world "; }
};
int main()
{
    std::thread t1{f};      // f() executes in separate thread
    std::thread t2{F()};    // F()() executes in separate thread
}  // spot the bugs
```

Neither thread is joined, so `std::thread`'s destructor calls `std::terminate`.
Adding the joins removes that, but not everything:

```cpp
void f() { std::cout << "Hello "; }
struct F {
    void operator()() const { std::cout << "parallel world "; }
};
int main()
{
    std::thread t1{f};      // f() executes in separate thread
    std::thread t2{F()};    // F()() executes in separate thread
    t1.join();
    t2.join();
}  // one bad bug left
```

The joins are ordinary statements, so anything that leaves the scope before
reaching them — an early return, an exception — is back to the first case.

Make "immortal threads" globals, put them in an enclosing scope, or put them on
the free store rather than detaching them. Do not detach. Because of old code
and third-party libraries using `std::thread`, this rule can be hard to
introduce.

## Decision

Use a thread type whose destructor joins.

- Prefer `gsl::joining_thread`, or its C++20 standard equivalent
  `std::jthread`, over `std::thread`.
- Do not rely on a `join()` call placed at the end of a scope; the destructor
  must be what joins, so that early returns and exceptions are covered.
- Where a thread must outlive its creating scope, give the thread object a
  longer-lived home — a global, an enclosing scope, or the free store — instead
  of detaching it.
- Where a `std::thread` must be used, because a third-party interface hands one
  back or expects one, make it obvious at the declaration whether it joins or
  detaches.

## Rationale

The two examples together are the argument. The first is a program that calls
`std::terminate` on exit with nothing in the source looking wrong; the second is
the fix that a careful reviewer would accept and that still fails on any path
that does not reach the last two statements.

A type whose destructor joins moves the guarantee from a statement, which is
conditional on control flow, into the object's lifetime, which is not. That is
the same move that resource handles make everywhere else, so it needs no
separate justification.

## Consequences

### Positive

- Removes the accidental `std::terminate` from a forgotten or skipped join.
- The join covers exceptional exits, which a written-out `join()` never does.
- Makes it visible at the declaration that this thread's lifetime is bounded by
  the scope.

### Negative

- A destructor that joins can block, and can do so during stack unwinding, where
  the wait is invisible and cancelling it is not an option.
- The type does not make the thread stop; it makes the scope wait. A worker that
  loops forever turns this into a hang, so the work still needs a stopping
  mechanism.
- It cannot be adopted at the boundary with libraries that hand out or expect a
  `std::thread`, so both types persist.
- `gsl::joining_thread` requires the GSL, so following the rule as written means
  taking on a library dependency; `std::jthread` avoids that but is not the type
  the guideline names.

### Neutral

- The rule is about which type is preferred, not a ban on `std::thread`; old
  code and third-party libraries make a clean sweep impractical.
- An immortal thread is legitimate; the rule is about where its object lives,
  not about how long it runs.

## Enforcement

- Flag uses of `std::thread`, and suggest `gsl::joining_thread` or C++20
  `std::jthread` instead.
- Suggest exporting ownership to an enclosing scope where the thread detaches.
- Warn where it is not obvious whether a thread joins or detaches.

## Related

- [0268](0268-cp-26-dont-detach-a-thread.md) — the companion rule; this type
  cannot detach.
- [0265](0265-cp-23-think-of-a-joining-thread-as-a-scoped-container.md) — the
  lifetime reasoning this type makes available.
- [0266](0266-cp-24-think-of-a-thread-as-a-global-container.md) — the much
  weaker reasoning available without it.
- [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
  — a thread is a resource, and this is the resource handle for it.
