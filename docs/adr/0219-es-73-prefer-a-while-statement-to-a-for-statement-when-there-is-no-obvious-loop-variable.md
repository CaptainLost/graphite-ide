# 0219. ES.73: Prefer a while-statement to a for-statement when there is no obvious loop variable

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Readability.

```cpp
int events = 0;
for (; wait_for_event(); ++events) {  // bad, confusing
    // ...
}
```

The event loop is misleading, because the `events` counter has nothing to do
with the loop condition, `wait_for_event()`. The for-header promises that the
thing being advanced is the thing being tested, and here it is not. Written as a
`while`, the counter goes where it belongs — with the work — and the loop's
control is the single condition it really has:

```cpp
int events = 0;
while (wait_for_event()) {      // better
    ++events;
    // ...
}
```

This is the converse of
[0218](0218-es-72-prefer-a-for-statement-to-a-while-statement-when-there-is-an-obvious-loop-variable.md).
Between them the two rules say: use the loop form whose shape matches the loop's
actual control structure.

## Decision

Use `while` when the loop is controlled by a condition rather than by a variable
being advanced.

- Write `while (cond) { ... }` for loops driven by an external event, by input,
  by a state check, or by anything else that is not a counter.
- Do not put an initialiser or an increment in a for-header unless it
  participates in the loop condition. A counter that merely accumulates belongs
  in the body.
- Where the loop is genuinely controlled by a variable, use `for`, per
  [0218](0218-es-72-prefer-a-for-statement-to-a-while-statement-when-there-is-an-obvious-loop-variable.md).
- Do not use `for (;;)` as an infinite loop when `while (true)` says the same
  thing more directly.

## Rationale

A for-header is a claim about how the loop makes progress. When the claim is
false — when the variable in the increment clause has nothing to do with the
condition — the reader has to discover that by reading the body, and the compact
form has bought nothing while costing a wrong first impression.

The counter in the example is not loop control at all; it is part of the work
the loop does. Putting it in the header does not merely misplace it, it
disguises an accumulator as a termination mechanism.

## Consequences

### Positive

- The loop's shape matches its control, so the header can be trusted.
- Accumulators end up next to the work they measure, where their meaning is
  visible.
- Together with
  [0218](0218-es-72-prefer-a-for-statement-to-a-while-statement-when-there-is-an-obvious-loop-variable.md),
  a reader can infer from the loop keyword alone whether progress is by counter
  or by condition.

### Negative

- The `while` form is exactly the one whose advance can be skipped by a
  `continue` or forgotten in a new branch, giving an infinite loop. This rule
  therefore reintroduces, for condition-driven loops, the hazard that
  [0218](0218-es-72-prefer-a-for-statement-to-a-while-statement-when-there-is-an-obvious-loop-variable.md)
  exists to remove.
- The variable that would have been in the for-initialiser is now declared in
  the enclosing scope and outlives the loop, which cuts against
  [0175](0175-es-5-keep-scopes-small.md).
- The enforcement — flag for-initialisers and for-increments that do not relate
  to the for-condition — will fire on legitimate multi-variable loops, such as
  walking two sequences in step, where the relation is real but not syntactic.
- "No obvious loop variable" is the same undefined judgement as in the converse
  rule, and the pair provides no test for deciding a borderline case.

### Neutral

- Nothing here concerns how the condition is computed; a `while` whose condition
  is an expensive call is a separate matter.

## Enforcement

Flag actions in for-initialisers and for-increments that do not relate to the
for-condition.

## Related

- [0218](0218-es-72-prefer-a-for-statement-to-a-while-statement-when-there-is-an-obvious-loop-variable.md)
  — the converse rule.
- [0221](0221-es-75-avoid-do-statements.md) — the third loop form, and the one
  to avoid.
- [0175](0175-es-5-keep-scopes-small.md) — the cost this rule accepts.
