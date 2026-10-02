# 0221. ES.75: Avoid do-statements

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Readability, and avoidance of errors. The termination condition is at the end,
where it can be overlooked, and the condition is not checked the first time
through.

```cpp
int x;
do {
    cin >> x;
    // ...
} while (x < 0);
```

Yes, there are genuine examples where a do-statement is a clear statement of a
solution, but there are also many bugs.

Two further things are visible in that short example. `x` is declared
uninitialised, which
[0183](0183-es-20-always-initialize-an-object.md) rules out, and it must be
declared outside the loop because the condition refers to it — so the
do-statement also forces the loop variable into the enclosing scope, against
[0220](0220-es-74-prefer-to-declare-a-loop-variable-in-the-initializer-part-of-a-for-statement.md).

## Decision

Prefer a `while` or a `for` to a `do`.

- Write the loop so the condition is checked before the body runs, and put the
  condition at the top where a reader looks for it.
- Where the body must run once before the condition can be evaluated, prefer to
  restructure so that the value the condition tests is obtained by the condition
  itself — a `while (read(x))` rather than a `do { read(x); } while (...)`.
- Do not use a `do` merely to avoid duplicating a first iteration; extract the
  body into a named function and call it from both places if that is the only
  obstacle.
- Where a `do` really is the clearest expression of the algorithm, keep it, and
  put the condition on the same line as the closing brace so it cannot be
  mistaken for the start of a new statement.
- Initialise every variable the loop uses at its declaration, per
  [0183](0183-es-20-always-initialize-an-object.md), which the `do` form makes
  easy to forget.

## Rationale

The distinguishing property of a `do` is not that its condition is at the
bottom but that its body is guaranteed to run once regardless of the condition.
That is occasionally the required semantics and much more often an accident, and
the two cases look identical, so a reader must reconstruct the author's intent
from the surrounding code.

The placement of the condition compounds it. In every other loop form, a reader
scanning for the loop's control finds it at the top; in a `do` it is at the
bottom, adjacent to a closing brace, and it is the single easiest piece of
control flow in C++ to read past.

## Consequences

### Positive

- The loop's condition is where readers look for it, in every loop in the
  codebase.
- The zero-iteration case is handled by construction rather than depending on
  the initial value of a variable set inside the body.
- Removes the pattern where a variable must be declared uninitialised outside
  the loop purely so the trailing condition can name it.

### Negative

- Read-then-test is a real shape — read a record, prompt for input, poll a
  device — and rewriting it usually costs either a duplicated first iteration or
  a `while (true)` with a `break` in the middle, which is not obviously better
  and is discouraged by the guidelines' own rule on minimising `break`.
- The rule concedes in its own Note that genuine examples exist, and then offers
  a blanket enforcement, so the enforcement and the guidance disagree.
- `do { ... } while (0)` is the standard idiom for a multi-statement macro, so a
  literal flag on every do-statement will fire on code where the construct is
  the correct and conventional answer.
- Flagging do-statements catches a construct, not a defect; the loops it flags
  are frequently correct, and the ones that are wrong are wrong for reasons the
  flag cannot see.

### Neutral

- Nothing here concerns the loop's body or its exit conditions beyond the
  placement of the test.
- A `do` retained deliberately is not a violation as long as the reason is
  visible; the record asks for a preference, not a ban.

## Enforcement

Flag do-statements.

## Related

- [0219](0219-es-73-prefer-a-while-statement-to-a-for-statement-when-there-is-no-obvious-loop-variable.md)
  — the form a condition-driven loop should take instead.
- [0218](0218-es-72-prefer-a-for-statement-to-a-while-statement-when-there-is-an-obvious-loop-variable.md)
  — the form a counter-driven loop should take instead.
- [0183](0183-es-20-always-initialize-an-object.md) — the uninitialised variable
  the do-statement in the example depends on.
