# 0228. ES.86: Avoid modifying loop control variables inside the body of raw for-loops

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

The loop control up front should enable correct reasoning about what is
happening inside the loop. Modifying loop counters in both the
iteration-expression and inside the body of the loop is a perennial source of
surprises and bugs.

```cpp
for (int i = 0; i < 10; ++i) {
    // no updates to i -- ok
}

for (int i = 0; i < 10; ++i) {
    //
    if (/* something */) ++i; // BAD
    //
}

bool skip = false;
for (int i = 0; i < 10; ++i) {
    if (skip) { skip = false; continue; }
    //
    if (/* something */) skip = true;  // Better: using two variables for two concepts.
    //
}
```

The third form costs an extra variable and gains a property the second does not
have: `i` still advances exactly once per iteration, so the loop header remains
a true statement about the number of iterations, and the decision to skip is
recorded in a variable that says what it means.

## Decision

Let the loop header own the loop control variable.

- Do not assign to, increment or decrement the loop control variable anywhere in
  the body of a raw `for` loop; the iteration-expression is its only writer.
- Where the body needs to influence the next iteration, introduce a separate
  variable for that concept and act on it at the top of the body.
- Do not pass the loop control variable to a function by non-`const` reference
  or pointer from inside the body.
- Prefer a range-`for`, an iterator, or a standard algorithm, where the control
  variable is not exposed to the body at all.

## Rationale

A `for` header is read as a summary: the initialiser, condition and
iteration-expression together are meant to tell you how many times the body runs
and over what. A write to the counter in the body silently invalidates that
summary, and the reader has no way to know without scanning the whole body — the
cost of which grows exactly as the loop gets long enough to matter.

Splitting the counter from the skip flag is the deeper point: two concepts,
"where am I" and "should this one be processed", were being carried by one
variable, which is
[0189](0189-es-26-dont-use-a-variable-for-two-unrelated-purposes.md) in the
small.

## Consequences

### Positive

- The loop header alone establishes the iteration count and the traversal order.
- Off-by-one and skipped-element bugs from a double increment disappear.
- The condition that causes an element to be skipped acquires a name, so the
  intent survives.

### Negative

- The recommended rewrite adds a variable and a guard clause at the top of every
  iteration, which is more code and one more thing to keep in sync — forgetting
  to clear the flag is its own bug, and the guidelines' own example has to clear
  it explicitly.
- Some algorithms genuinely need a variable stride: run-length decoding, escape
  sequence scanning, and parsing generally consume a variable number of items
  per step. Expressing those without touching the index means restructuring
  around a `while` loop or an iterator, not a mechanical fix.
- The extra `continue` runs against
  [0223](0223-es-77-minimize-the-use-of-break-and-continue-in-loops.md), so the
  two rules cannot both be satisfied by the suggested form.

### Neutral

- The rule is scoped to raw `for` loops; a `while` loop makes no promise in its
  header and is not covered.
- Range-`for` and algorithms sidestep the question entirely, which is the
  outcome the guidelines prefer in any case.

## Enforcement

Flag variables that are potentially updated — that have a non-`const` use — in
both the loop control iteration-expression and the loop body.

## Related

- [0189](0189-es-26-dont-use-a-variable-for-two-unrelated-purposes.md) — the
  general form of the counter-plus-flag conflation.
- [0223](0223-es-77-minimize-the-use-of-break-and-continue-in-loops.md) — this
  rule prefers `break` to modifying the loop variable, and its own fix uses
  `continue`.
- [0176](0176-es-6-declare-names-in-for-statement-initializers-and-conditions-to-limit-scope.md)
  — the other rule about what belongs in a `for` header.
- [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md)
  — the algorithms that remove the loop control variable altogether.
