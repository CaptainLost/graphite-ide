# 0275. CP.44: Remember to name your lock_guards and unique_locks

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

An unnamed local object is a temporary that immediately goes out of scope.

```cpp
unique_lock<mutex>(m1);
lock_guard<mutex> {m2};
lock(m1, m2);
```

This looks innocent enough, but it is not. The first line does not create a
guard on `m1` at all: it is a declaration of a variable named `m1`, with the
redundant parentheses making it look like a call. The second creates a temporary
guard that locks `m2` and unlocks it at the end of that statement, leaving the
following code unprotected while appearing to be inside a critical section.

## Decision

Give every lock object a name.

- Declare a scoped lock as a named local: `std::scoped_lock lck {m};`, never as
  a bare `std::scoped_lock {m};` or `std::lock_guard<std::mutex>(m);`.
- Read a lock declaration as a declaration. If the code does not name the lock,
  it does not hold one for the following statements.
- Where a name is genuinely uninteresting, still supply one; a short
  conventional name such as `lck` is enough.
- Do not treat the presence of a lock type in the source as evidence that a
  region is protected; look for the name and the scope it lives in.

## Rationale

This is a syntactic accident with a silent, catastrophic effect. Both broken
forms compile, both mention a mutex and a lock type, and both leave the
following code running unsynchronised — so the defect passes review by looking
exactly like the correct code.

The first line is worse than the second, because it is not a lock at all. Adding
a name is the entire fix, and it is why this small rule stands next to the rule
that requires RAII locking: RAII is only protection when the object it creates
has a lifetime longer than the statement that made it.

## Consequences

### Positive

- Closes the one hole in scope-based locking that compiles, reviews cleanly and
  protects nothing.
- Disambiguates the vexing-parse form, where a lock declaration is silently a
  variable declaration instead.
- Costs nothing: a name and a few characters.

### Negative

- The names are usually meaningless, so the rule adds identifiers that carry no
  information and can be mistaken for something worth reading.
- It is a rule about a language wart rather than about design, so it must be
  remembered rather than derived.
- Naming alone does not make the scope correct; a named lock in too wide or too
  narrow a block is still wrong, and this rule says nothing about that.
- The check is keyed to the standard lock types, so a hand-written RAII lock
  type is not covered by it and the same silent failure remains available there.

### Neutral

- Applies equally to `lock_guard`, `unique_lock`, `scoped_lock` and any other
  RAII lock type.
- The rule is mechanically checkable, so it need not rely on discipline once a
  checker is in place.

## Enforcement

- Flag all unnamed `lock_guard`s and `unique_lock`s.

## Related

- [0262](0262-cp-20-use-raii-never-plain-lock-unlock.md) — the rule this one
  completes; an unnamed guard is RAII that guards nothing.
- [0263](0263-cp-21-use-std-lock-or-std-scoped-lock-to-acquire-multiple-mutexes.md)
  — the `lock(m1, m2)` in the example, and the guards that must adopt it.
- [0274](0274-cp-43-minimize-time-spent-in-a-critical-section.md) — the scope
  the name is attached to is what bounds the critical section.
- [0186](0186-es-23-prefer-the-initializer-syntax.md) — the initialisation
  syntax, and the parse ambiguity that makes the first example a declaration
  rather than a lock.
