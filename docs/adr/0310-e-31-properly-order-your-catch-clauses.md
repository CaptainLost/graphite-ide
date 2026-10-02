# 0310. E.31: Properly order your catch-clauses

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

`catch`-clauses are evaluated in the order they appear, and one clause can hide
another.

```cpp
void f()
{
    // ...
    try {
            // ...
    }
    catch (Base& b) { /* ... */ }
    catch (Derived& d) { /* ... */ }
    catch (...) { /* ... */ }
    catch (std::exception& e) { /* ... */ }
}
```

If `Derived` is derived from `Base`, the `Derived` handler will never be
invoked: the `Base` handler matches first, and matching is by order rather than
by best fit. The last two clauses show the same defect in its extreme form,
where a catch-all would leave the `std::exception` handler unreachable — though
that particular arrangement does not survive compilation, since the language
requires a `...` handler to be the last handler for its try block.

This is different from overload resolution, and that is the trap. Everywhere
else in C++ the most specific candidate wins; here the first candidate that can
match does. A reader who applies the intuition acquired from overloading will
read this function as handling `Derived` specially, and it does not.

Between typed handlers, the consequence is not a compile error and usually not a
crash. The program runs the wrong handler, which by construction is the more
general one, so it does something plausible — logs a generic message, returns a
generic failure — and the specific recovery that was written is simply dead.
Some compilers warn about a base handler placed before a derived one, but
nothing requires them to, and a warning is easily lost. Dead handler code is
particularly hard to notice because handlers are rarely exercised in the first
place.

The ordering rule follows directly: derived before base, specific before
general, and `catch (...)` last if it appears at all. `catch (...)` is not a
default clause; it is an ordinary clause that happens to match everything, and
the only reason a misplaced one is not a silent hazard like the rest is that the
language rejects it outright.

## Decision

Order `catch` clauses from most derived to most general.

- Put a handler for a derived exception type before a handler for any of its
  bases.
- Put `catch (...)` last. The language requires it to be the last handler for
  its try block, so a clause written after it is a compile error rather than
  dead code.
- Order handlers for unrelated types by whichever reads best; only related types
  can hide one another.
- Where a handler distinguishes several types from a library, list the specific
  types it can usefully act on first, then `catch (const std::exception&)`, then
  `catch (...)` if it is wanted at all.
- Treat an unreachable handler as a defect to be fixed, not as harmless
  redundancy. It represents recovery someone intended and is not getting.

## Rationale

The rule exists because `catch` matching is first-match rather than best-match,
which contradicts the resolution rule that applies everywhere else in the
language. That mismatch is why the mistake is made by people who understand C++
well.

The failure is silent in both directions: it compiles, and it runs a handler
that looks reasonable. Nothing in the program's behaviour distinguishes "the
specific handler ran" from "the general handler ran instead", so the defect can
survive indefinitely in code that is by its nature rarely executed.

## Consequences

### Positive

- Every handler that is written can actually run.
- The reading order of the clauses matches the order in which they are tried, so
  the code says what it does.
- A catch-all placed last is unambiguously a fallback rather than an accidental
  cut-off.

### Negative

- The rule is fragile under edit: adding a clause in the middle, or introducing
  a base-class relationship between two exception types that previously had
  none, can make an existing handler unreachable without touching it.
- Correct ordering requires knowing the inheritance relationships among all the
  types caught, which for a third-party library means reading its headers.
- Specific-first ordering conflicts with readability when there are many
  clauses; the most derived handler ends up first regardless of how rare its
  case is or how little it has to say.
- Handlers are among the least exercised code in a program, so an ordering
  mistake can survive indefinitely without any run producing a symptom that
  points at it.

### Neutral

- The rule matters only where the caught types are related; unrelated clauses
  cannot hide one another and may be ordered freely.
- Fewer handlers means fewer opportunities to get this wrong, which is a further
  argument for
  [0303](0303-e-18-minimize-the-use-of-explicit-try-catch.md).

## Enforcement

- Flag all hiding handlers — any `catch` clause that cannot be reached because
  an earlier clause matches everything it would.

## Related

- [0300](0300-e-15-throw-by-value-catch-exceptions-from-a-hierarchy-by-reference.md)
  — the other requirement on catching from a hierarchy.
- [0299](0299-e-14-use-purpose-designed-user-defined-types-as-exceptions-not-built-in-types.md)
  — the hierarchy whose ordering this governs.
- [0303](0303-e-18-minimize-the-use-of-explicit-try-catch.md) — the fewer
  handlers there are, the less this can go wrong.
