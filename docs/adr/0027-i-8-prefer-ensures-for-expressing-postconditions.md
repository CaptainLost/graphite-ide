# 0027. I.8: Prefer Ensures() for expressing postconditions

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

The reason mirrors the precondition case: to make it clear that a condition is a
postcondition, and to enable tool use.

```cpp
void f()
{
    char buffer[MAX];
    // ...
    memset(buffer, 0, MAX);
    Ensures(buffer[0] == 0);
}
```

Postconditions can be stated in many ways, including comments, `if` statements
and `assert()`. That makes them hard to distinguish from ordinary code, hard to
update, hard to manipulate by tools, and they might have the wrong semantics.

Postconditions of the form "this resource must be released" are best expressed
by RAII rather than by any assertion.

Ideally the `Ensures` would be part of the interface, but that is not easily
done; for now it is placed in the definition. Once language support becomes
available — the contract proposal — the standard version of preconditions,
postconditions and assertions will be adopted.

## Decision

Express postconditions with a dedicated postcondition facility — `Ensures()` or
an equivalent facility — rather than with `assert()`, a bare `if`, or a
comment.

- Use it for conditions that must hold at return, and keep it distinct from
  ordinary error handling.
- Do not use an assertion where RAII can make the postcondition hold by
  construction; a released resource is guaranteed by a guard object, not checked
  by a condition.
- When standard contract support arrives, migrate to it.

## Rationale

Same argument as for preconditions: a postcondition written as an `if` is
indistinguishable from logic, and a postcondition written as a comment is
invisible to everything except a careful reader. A dedicated facility makes the
category searchable and its policy configurable in one place.

The RAII carve-out is the important half. An assertion that the mutex was
unlocked fires after the mistake; a `lock_guard` makes the mistake
unexpressible, which is strictly better and should be preferred wherever the
postcondition is about a resource.

## Consequences

### Positive

- Postconditions are distinguishable from logic and from error handling.
- One policy governs what a violation does, across the codebase.
- The RAII preference converts a class of postcondition into a guarantee.

### Negative

- The assertion lives in the definition, so callers reading the header do not
  see it.
- Whether the check survives into a release build is a configuration question
  left open here.
- A postcondition that is expensive to evaluate — checking a whole container is
  sorted, say — cannot be asserted without changing the cost of the function, so
  the most valuable postconditions are often the ones that cannot be checked.

### Neutral

- Not mechanically enforceable, for the same reason as the precondition case.
- This record chooses a form; whether a function has a postcondition worth
  stating is [0026](0026-i-7-state-postconditions.md).

## Enforcement

(Not enforceable.) Finding the variety of ways postconditions can be asserted is
not feasible. Warning about those that can be easily identified — `assert()` —
has questionable value in the absence of a language facility.

## Related

- [0026](0026-i-7-state-postconditions.md) — the rule this gives a form to.
- [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md) — the
  symmetric decision.
- [0014](0014-p-8-do-not-leak-any-resources.md) — RAII, the preferred mechanism
  for release postconditions.
