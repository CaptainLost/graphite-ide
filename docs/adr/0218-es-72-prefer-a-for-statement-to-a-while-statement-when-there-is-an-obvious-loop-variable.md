# 0218. ES.72: Prefer a for-statement to a while-statement when there is an obvious loop variable

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Readability: the complete logic of the loop is visible up front, and the scope
of the loop variable can be limited.

```cpp
for (gsl::index i = 0; i < vec.size(); i++) {
    // do work
}
```

against:

```cpp
int i = 0;
while (i < vec.size()) {
    // do work
    i++;
}
```

The two loops do the same thing. In the first, the initialisation, the condition
and the increment are on one line, so a reader can check the loop's control
without reading the body at all. In the second, the increment is somewhere
inside the body, the variable outlives the loop, and whether the loop terminates
cannot be established without reading everything between.

## Decision

Where a loop has an obvious loop variable — something initialised before the
loop, tested each time round and advanced each time round — use a `for`
statement.

- Put the initialisation, the condition and the advance in the for-header, so
  all three are visible together.
- Declare the loop variable in the initialiser, so its scope ends with the loop;
  see
  [0220](0220-es-74-prefer-to-declare-a-loop-variable-in-the-initializer-part-of-a-for-statement.md).
- Do not advance the loop variable in the body of a `for`; that is what the
  third clause of the header is for.
- Where there is no such variable, use `while`, per
  [0219](0219-es-73-prefer-a-while-statement-to-a-for-statement-when-there-is-no-obvious-loop-variable.md).
- Where the loop simply visits every element of a sequence, neither form is
  right; use a range-for, per
  [0217](0217-es-71-prefer-a-range-for-statement-to-a-for-statement-when-there-is-a-choice.md).

## Rationale

The argument is about where the termination proof lives. A `for` header contains
the whole of it, so a reader checking that the loop terminates reads one line;
the `while` version distributes the same proof across three places and leaves it
vulnerable to an early `continue` that skips the increment — a bug that cannot
be written in the `for` form at all.

The scope argument is the second half. The `while` version leaves `i` alive
after the loop, where it can be read by mistake or reused for something
unrelated, which
[0175](0175-es-5-keep-scopes-small.md) rules out for good reasons.

## Consequences

### Positive

- Termination can be checked without reading the body.
- The loop variable's scope ends with the loop, so it cannot be read afterwards
  or accidentally reused.
- The `continue`-skips-the-increment infinite loop becomes unexpressible.

### Negative

- "Obvious loop variable" is a judgement, and this record and
  [0219](0219-es-73-prefer-a-while-statement-to-a-for-statement-when-there-is-no-obvious-loop-variable.md)
  together do nothing but push every borderline loop onto that judgement. The
  upstream rule offers no enforcement at all.
- Where the loop's final index is needed afterwards, the loop variable cannot be
  declared in the header, so the rule and
  [0220](0220-es-74-prefer-to-declare-a-loop-variable-in-the-initializer-part-of-a-for-statement.md)
  pull in opposite directions and one of them gives way.
- Loops with two or three control variables produce a for-header full of comma
  operators that is considerably less readable than the `while` form it
  replaced.
- The rule says nothing about the far more common improvement, which is to
  replace the indexed loop entirely.

### Neutral

- The upstream example uses `i++` where `++i` is the usual advice for
  non-trivial types; nothing in this record depends on which is written.

## Related

- [0219](0219-es-73-prefer-a-while-statement-to-a-for-statement-when-there-is-no-obvious-loop-variable.md)
  — the converse rule; the two are meant to be read together.
- [0217](0217-es-71-prefer-a-range-for-statement-to-a-for-statement-when-there-is-a-choice.md)
  — the form that beats both where it applies.
- [0220](0220-es-74-prefer-to-declare-a-loop-variable-in-the-initializer-part-of-a-for-statement.md)
  — where the loop variable is declared.
- [0175](0175-es-5-keep-scopes-small.md) — the scope argument behind the
  preference.
