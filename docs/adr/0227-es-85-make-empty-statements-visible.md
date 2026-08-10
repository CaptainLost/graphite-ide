# 0227. ES.85: Make empty statements visible

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Readability. A lone semicolon is the smallest possible statement and the easiest
possible thing to miss:

```cpp
for (i = 0; i < max; ++i);   // BAD: the empty statement is easily overlooked
v[i] = f(v[i]);

for (auto x : v) {           // better
    // nothing
}
v[i] = f(v[i]);
```

In the first version the loop body is empty and `v[i] = f(v[i])` runs once,
after the loop; in a version without the stray semicolon it would run `max`
times. Nothing on the page distinguishes the two but one character.

## Decision

Write an empty statement as an empty block, not as a bare semicolon.

- Where a loop legitimately has no body, write `{}` — with a comment saying why
  it is empty — instead of terminating the loop header with `;`.
- Never leave a semicolon immediately after the closing parenthesis of an `if`,
  `for` or `while` header.
- Where an empty body is not intended, remove the semicolon; the following
  statement was almost certainly meant to be the body.
- Prefer restructuring so that the empty body disappears altogether, for example
  by moving the work into the loop's iteration-expression or by using an
  algorithm.

## Rationale

This is the cheapest instance of the general principle that intent must be
visible: an empty block states "there is deliberately nothing here", whereas a
semicolon states nothing at all and is indistinguishable from a typo. Because
the mistake is a single character in a legal position, no compiler diagnostic is
guaranteed and no amount of careful reading is reliable.

A blanket requirement that every control-flow body be brace-delimited subsumes
this rule: under such a convention an empty body is written `{}` and the bare
semicolon never arises in the first place.

## Consequences

### Positive

- The difference between "no body" and "forgot the body" becomes visible at a
  glance.
- The comment inside the empty block records why the loop has no work to do,
  which the semicolon form cannot.
- Consistent with any convention that already braces control-flow bodies, so no
  new formatting judgement is needed.

### Negative

- Three lines where one character sufficed, for a construct that is already
  rare.
- Loops with genuinely empty bodies are usually terse idioms — spin waits,
  pointer-advancing scans — where the expanded form reads as heavier than the
  operation it describes.
- Does not catch the more common variant of the mistake, a stray semicolon after
  an `if` condition where the intended body is a single statement that then runs
  unconditionally, unless a tool is looking for it.

### Neutral

- Whether a loop should have an empty body at all is a separate design question.
- The rule concerns statements, not empty function or class bodies, which are
  ordinary.

## Enforcement

Flag empty statements that are not blocks and do not contain comments.

## Related

- [0009](0009-p-3-express-intent.md) — an empty block states an intent a
  semicolon cannot.
- [0228](0228-es-86-avoid-modifying-loop-control-variables-inside-the-body-of-raw-for-loops.md)
  — the other rule about loop bodies not matching their headers.
- [0197](0197-es-40-avoid-complicated-expressions.md) — the loops whose work has
  been packed into the header instead of the body.
