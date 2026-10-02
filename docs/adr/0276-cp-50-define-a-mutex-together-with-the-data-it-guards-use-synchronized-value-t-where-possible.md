# 0276. CP.50: Define a mutex together with the data it guards. Use synchronized_value<T> where possible

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

It should be obvious to a reader that data is to be guarded, and how. Defining
the mutex next to the data decreases the chance of the wrong mutex being locked,
or of the mutex not being locked at all.

Using a `synchronized_value<T>` goes further: it ensures that the data has a
mutex, and that the right mutex is locked when the data is accessed. This is a
WG21 proposal to add `synchronized_value` to a future technical specification or
revision of the standard, not a facility available in C++20.

```cpp
struct Record {
    std::mutex m;   // take this mutex before accessing other members
    // ...
};
class MyClass {
    struct DataRecord {
       // ...
    };
    synchronized_value<DataRecord> data; // Protect the data with a mutex
};
```

## Decision

Never let a mutex float free of the data it protects.

- Declare the mutex as a member of the same class or struct as the data it
  guards, immediately adjacent to it, and say in a comment or an annotation what
  it covers.
- Do not declare a mutex at namespace scope to guard data declared elsewhere;
  wrap the data and the mutex in a type instead.
- Where a wrapper that binds the two is available — a `synchronized_value`-like
  template, or a small hand-written type that hands out the data only through a
  locked accessor — prefer it, because it makes the wrong access fail to compile
  rather than fail at run time.
- Keep the guarded members private, so that every access goes through code that
  can take the lock.
- Where the toolchain supports thread-safety annotations, use them to state the
  mutex-to-data relationship in a form the compiler can check.

## Rationale

A free-standing mutex encodes its meaning in a convention, and a convention is
invisible at the point of use. Nothing about touching a member tells the reader
that some particular mutex should be held first, so the two ways of getting it
wrong — locking the wrong mutex, and locking none — both look exactly like
correct code.

The stronger form is worth the trouble because it changes the category of the
error. Pairing them by placement makes the convention legible; wrapping them in
a type that only yields access under the lock makes the convention unnecessary.

## Consequences

### Positive

- The relationship between lock and data is stated where both are declared,
  rather than in a comment far away or nowhere.
- Locking the wrong mutex becomes much harder, and with a wrapper type it can be
  made impossible.
- The pairing is exactly what static thread-safety annotations express, so this
  form is the one tooling can check.

### Negative

- `synchronized_value` is a proposal, not a standard facility; adopting the
  strong form means writing and maintaining the wrapper here, or taking a
  dependency for it.
- A wrapper that only yields access under a lock makes multi-step operations
  awkward, because each access re-locks unless the type offers a way to hold the
  lock across several — and that escape hatch reintroduces what it prevented.
- One mutex per object costs space in every instance and can be far
  finer-grained than the workload needs.
- Embedding a mutex makes the type non-copyable and non-movable by default,
  which changes the value semantics of what may have been a plain data type.

### Neutral

- The rule does not say the mutex must be a member in every case; a small
  wrapper type holding both is equally valid, and often better.
- Where the wrapper form is not available, the placement-and-comment form is
  still the rule.

## Enforcement

- The upstream rule marks enforcement as unresolved, questioning whether it is
  possible at all. In practice the checkable part is the thread-safety
  annotation, where the toolchain supports it.

## Related

- [0257](0257-cp-2-avoid-data-races.md) — the failure that a mislocked or
  unlocked access produces.
- [0262](0262-cp-20-use-raii-never-plain-lock-unlock.md) — how the mutex named
  here is taken.
- [0261](0261-cp-9-whenever-feasible-use-tools-to-validate-your-concurrent-code.md)
  — the static annotations that express exactly this pairing.
- [0040](0040-c-1-organize-related-data-into-structures-structs-or-classes.md) —
  a mutex and the data it guards are related data.
