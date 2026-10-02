# 0263. CP.21: Use std::lock() or std::scoped_lock to acquire multiple mutexes

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Taking more than one mutex one at a time is asking for deadlock.

```cpp
// thread 1
lock_guard<mutex> lck1(m1);
lock_guard<mutex> lck2(m2);
// thread 2
lock_guard<mutex> lck2(m2);
lock_guard<mutex> lck1(m1);
```

Instead, use `lock()`:

```cpp
// thread 1
lock(m1, m2);
lock_guard<mutex> lck1(m1, adopt_lock);
lock_guard<mutex> lck2(m2, adopt_lock);
// thread 2
lock(m2, m1);
lock_guard<mutex> lck2(m2, adopt_lock);
lock_guard<mutex> lck1(m1, adopt_lock);
```

Or, better but C++17 only:

```cpp
// thread 1
scoped_lock<mutex, mutex> lck1(m1, m2);
// thread 2
scoped_lock<mutex, mutex> lck2(m2, m1);
```

Here the writers of thread 1 and thread 2 still do not agree on the order of the
mutexes, but the order no longer matters.

Two notes qualify how easy this is to spot in practice. In real code, mutexes
are rarely named in a way that conveniently reminds the programmer of an
intended relation and an intended order of acquisition, and they are not always
conveniently acquired on consecutive lines. In C++17 it is also possible to
write plain `lock_guard lck1(m1, adopt_lock);` and have the mutex type deduced.

## Decision

Acquire several mutexes in one operation, never one after another.

- Use `std::scoped_lock` with all the mutexes as arguments; it is the C++17 form
  and the one to prefer wherever that standard is available.
- Where a `scoped_lock` cannot be used, call `std::lock()` with all the mutexes
  and then adopt each one into its own guard with `std::adopt_lock`.
- Do not rely on every site agreeing on an acquisition order; a deadlock-free
  algorithm is preferable to a convention that a future call site can break.
- Take all the mutexes a critical section needs at its start, rather than
  acquiring a second one part-way through while the first is held.
- Do not depend on mutex names to convey an intended ordering.

## Rationale

The ordering convention is a global invariant maintained by local edits, which
is the shape of rule that always eventually fails. It requires every site that
takes both mutexes to agree, forever, including the sites where the two
acquisitions are hundreds of lines apart and the names give no hint that a
relation exists.

`scoped_lock` replaces that invariant with an algorithm. The two threads in the
example still disagree about the order, and the code is still correct, which is
precisely the property a convention cannot offer.

## Consequences

### Positive

- Deadlock between the acquisitions becomes impossible rather than merely
  discouraged.
- Call sites no longer need to know a global ordering, so a new one cannot break
  an old one.
- The C++17 form is a single declaration, so it is shorter than the ordered
  guards it replaces.

### Negative

- It only covers mutexes taken together in one statement. A second mutex
  acquired while a first is already held — through a nested call, or across a
  callback — is exactly the case this cannot see, and that is the common shape
  of real deadlocks.
- Acquiring everything up front means holding some mutexes longer than needed,
  which works against keeping critical sections short.
- The `std::lock` plus `adopt_lock` form is verbose and easy to get wrong, and
  it is still needed wherever a single scope will not do.
- The algorithm may back off and retry, so the cost of taking several mutexes
  together is not simply the sum of taking them separately.

### Neutral

- Class template argument deduction makes the explicit mutex type list
  unnecessary in C++17 and later; the fully spelled form in the example is not
  required.
- The rule says nothing about how many mutexes a design should have, only how to
  take the ones it has.

## Enforcement

- Detect the acquisition of multiple mutexes. This is undecidable in general,
  but catching common simple examples, like the one above, is easy.

## Related

- [0262](0262-cp-20-use-raii-never-plain-lock-unlock.md) — the guards this rule
  builds on.
- [0264](0264-cp-22-never-call-unknown-code-while-holding-a-lock-e-g-a-callback.md)
  — the deadlock this rule cannot prevent.
- [0274](0274-cp-43-minimize-time-spent-in-a-critical-section.md) — the pressure
  that pulls the other way.
- [0276](0276-cp-50-define-a-mutex-together-with-the-data-it-guards-use-synchronized-value-t-where-possible.md)
  — knowing which mutexes an operation needs depends on knowing what each
  guards.
