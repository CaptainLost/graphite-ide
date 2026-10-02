# 0224. ES.78: Don't rely on implicit fallthrough in switch statements

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Always end a non-empty case with a `break`. Accidentally leaving one out is a
fairly common bug, and a deliberate fallthrough can be a maintenance hazard, so
it should be rare and explicit.

```cpp
switch (eventType) {
case Information:
    update_status_bar();
    break;
case Warning:
    write_event_log();
    // Bad - implicit fallthrough
case Error:
    display_error_window();
    break;
}
```

Multiple case labels on a single statement are fine, because nothing is being
skipped:

```cpp
switch (x) {
case 'a':
case 'b':
case 'f':
    do_something(x);
    break;
}
```

Return statements in a case label are also fine:

```cpp
switch (x) {
case 'a':
    return 1;
case 'b':
    return 2;
case 'c':
    return 3;
}
```

In the rare cases where fallthrough is deemed appropriate, be explicit and use
the `[[fallthrough]]` annotation:

```cpp
switch (eventType) {
case Information:
    update_status_bar();
    break;
case Warning:
    write_event_log();
    [[fallthrough]];
case Error:
    display_error_window();
    break;
}
```

## Decision

End every non-empty case with a statement that leaves the switch, and say so
when you mean to fall through.

- Terminate each non-empty case with `break`, `return`, `throw`, or `continue`.
- Where control is meant to continue into the next case, write
  `[[fallthrough]];` as the last statement of the case.
- Stacked case labels sharing one statement group need no annotation; that is
  not fallthrough.
- Do not use a comment such as `// falls through` in place of the attribute — a
  comment carries no meaning for the compiler and does not silence the warning.
- Treat a `[[fallthrough]]` as something a reviewer should question, not as a
  routine construct.

## Rationale

The failure mode here is asymmetric. Omitting a `break` produces code that
compiles, runs, and does something plausible-looking on some inputs, and the
defect is invisible at the point of the mistake — there is nothing to see, only
something missing. `[[fallthrough]]` inverts that: the intended case becomes
visible and the unintended one becomes a diagnosable absence, which is
[0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) applied
to a statement form.

Making the deliberate case explicit also costs nothing at run time; the
attribute exists solely so the compiler can tell the two apart.

## Consequences

### Positive

- The most common switch bug becomes something the compiler can warn about.
- Deliberate fallthrough is documented in the code, in a form that cannot drift
  out of date the way a comment can.
- Reviewers can check each case in isolation rather than tracing control across
  case boundaries.

### Negative

- Correct code becomes more verbose: state machines and character classifiers
  that chain cases pick up an annotation on every link.
- The annotation is only meaningful as the last statement before a case label,
  so it cannot document a conditional fallthrough, which is where the confusing
  cases actually live.
- Compiler warnings for missing `break` are heuristic and are typically silenced
  by any exit statement, so the rule is better enforced than the naive form
  suggests but still not complete.

### Neutral

- The empty-case pattern of stacked labels is untouched, and remains the normal
  way to give several values one action.
- Whether the switch itself is the right construct is a separate question, and
  [0225](0225-es-79-use-default-to-handle-common-cases-only.md) governs what
  happens to the values no case names.

## Enforcement

Flag all implicit fallthroughs from non-empty cases.

## Related

- [0225](0225-es-79-use-default-to-handle-common-cases-only.md) — the companion
  rule about the `default` label of the same switch.
- [0223](0223-es-77-minimize-the-use-of-break-and-continue-in-loops.md) — the
  same keyword, the other binding.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — why an
  attribute the compiler understands beats a comment.
