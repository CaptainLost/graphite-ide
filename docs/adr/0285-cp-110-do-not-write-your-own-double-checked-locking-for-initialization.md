# 0285. CP.110: Do not write your own double-checked locking for initialization

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Since C++11, static local variables are initialised in a thread-safe way. When
combined with the RAII pattern, static local variables can replace the need for
writing your own double-checked locking for initialisation. `std::call_once` can
achieve the same purpose. Use either the thread-safe static local variables of
C++11 or `std::call_once` instead of writing your own double-checked locking for
initialisation.

With `std::call_once`:

```cpp
void f()
{
    static std::once_flag my_once_flag;
    std::call_once(my_once_flag, []()
    {
        // do this only once
    });
    // ...
}
```

With a thread-safe static local variable:

```cpp
void f()
{
    // Assuming the compiler is compliant with C++11
    static My_class my_object; // Constructor called only once
    // ...
}

class My_class
{
public:
    My_class()
    {
        // do this only once
    }
};
```

## Decision

Let the language do the once-only initialisation.

- Where a value must be constructed once and then shared, declare it as a
  function-local `static` and let its constructor do the work.
- Where the once-only action is not the construction of an object, use
  `std::call_once` with a `std::once_flag` that outlives every caller.
- Do not write the flag-check, lock, flag-check-again sequence yourself for
  initialisation, in any of its spellings.
- Do not reach for a hand-written variant on the grounds that the compiler's
  guard is slow; that is a measurement to be taken, not assumed.

## Rationale

Hand-written double-checked locking is an attempt to reimplement a guarantee the
compiler already gives, and has given correctly on every conforming
implementation since C++11. The hand-written version is not merely redundant; it
is the canonical example of code that appears obviously correct, is not, and
fails only under contention.

## Consequences

### Positive

- The thread-safety of the initialisation is the implementation's problem, and
  it has already solved it.
- The intent reads directly: a `static` local says "constructed once, on first
  use" with no supporting machinery.
- RAII composes with it, so the constructed object's own resources are managed
  by the same declaration.
- It removes the single most common reason to write lock-free code by hand.

### Negative

- Every entry to the function pays a guard check, small but not free, and the
  rule offers no escape for a genuinely hot path other than not using a static
  there.
- Initialisation now happens on whichever thread reaches the function first, and
  at whatever moment that is; you give up control of when and where the
  constructor runs.
- A constructor that can fail has nowhere good to put the failure. An
  initialisation that exits by throwing does not count as done, so the exception
  reaches whichever thread happened to arrive first and the next caller retries
  the construction; lazily initialising something fallible therefore needs a
  different shape — an explicitly initialised object behind an accessor that can
  report an error — which this rule does not supply.
- Making once-only initialisation easy makes process-wide singletons easy, which
  [0022](0022-i-3-avoid-singletons.md) argues against; the rule fixes the
  mechanism without endorsing the design.

### Neutral

- The two permitted mechanisms are not equivalent: one initialises an object,
  the other performs an action. Choose by which is actually wanted.
- [0286](0286-cp-111-use-a-conventional-pattern-if-you-really-need-double-checked-locking.md)
  covers the cases the guidelines accept as not being violations of this rule.

## Related

- [0286](0286-cp-111-use-a-conventional-pattern-if-you-really-need-double-checked-locking.md)
  — what to do in the narrow case where this rule does not apply.
- [0282](0282-cp-100-dont-use-lock-free-programming-unless-you-absolutely-have-to.md)
  — the general rule of which this is the most common specific instance.
- [0022](0022-i-3-avoid-singletons.md) — the function-local static is the
  singleton mechanism; this rule is not a reason to want one.
- [0033](0033-i-22-avoid-complex-initialization-of-global-objects.md) — lazy
  initialisation is the alternative to complex initialisation before `main`.
