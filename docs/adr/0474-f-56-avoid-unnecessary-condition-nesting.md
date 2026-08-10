# 0474. F.56: Avoid unnecessary condition nesting

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Shallow nesting of conditions makes the code easier to follow. It also makes the
intent clearer. Strive to place the essential code at the outermost scope,
unless this obscures intent.

Use a guard clause to take care of exceptional cases and return early:

```cpp
// Bad: Deep nesting
void foo() {
    ...
    if (x) {
        computeImportantThings(x);
    }
}
```

```cpp
// Bad: Still a redundant else.
void foo() {
    ...
    if (!x) {
        return;
    }
    else {
        computeImportantThings(x);
    }
}
```

```cpp
// Good: Early return, no redundant else
void foo() {
    ...
    if (!x)
        return;

    computeImportantThings(x);
}
```

The same applies to conditions that are nested only because they were written
one at a time:

```cpp
// Bad: Unnecessary nesting of conditions
void foo() {
    ...
    if (x) {
        if (y) {
            computeImportantThings(x);
        }
    }
}
```

```cpp
// Good: Merge conditions + return early
void foo() {
    ...
    if (!(x && y))
        return;

    computeImportantThings(x);
}
```

## Decision

Keep the body of a function at the shallowest nesting the logic allows.

- Handle the cases that do nothing first, with a guard clause that returns, and
  leave the substance of the function unindented at the outermost level.
- Never write an `else` after a block whose every path returns, breaks,
  continues, or throws. The `else` adds a level and no information.
- Merge conditions that are nested only in sequence — `if (x) { if (y) {`
  becomes one condition — unless the two tests have genuinely different meanings
  that the separation records.
- Do not apply the rule mechanically where it hurts. A merged condition that
  nobody can read, or a chain of early returns that hides the function's normal
  path, is a worse result than one level of nesting.

## Rationale

The reason to prefer the flat form is that indentation is a claim about
structure, and unnecessary nesting makes a false one. Two levels of `if` around
the only statement that matters says the function has cases; a guard clause and
a body says it has preconditions and work, which is what is actually true.

Early return also localises the reasoning. Once a guard has returned, every line
below it can be read knowing the guarded condition holds, and the reader never
has to keep track of which brace closes which case in order to know where they
are.

## Consequences

### Positive

- The essential work sits at one indent level, so it can be found without
  reading the conditions.
- Each guard clause makes an assumption explicit at the point where it starts to
  hold, which is a documented precondition in executable form.
- Adding another exclusion later means adding one line at the top rather than
  restructuring a nest.
- Line length stops being consumed by indentation, which matters at the
  80-column and 120-column limits code is usually held to.

### Negative

- Multiple returns make it harder to see all exits from a function, and harder
  to place code that must run on every path — which is a real cost in code that
  is not fully RAII, though
  [0014](0014-p-8-do-not-leak-any-resources.md) removes most of it.
- Merging `if (x) { if (y) {` into `if (!(x && y))` inverts the condition, and
  an inverted compound condition is harder to read than the two positive ones it
  replaces; De Morgan mistakes made here are silent.
- The rule can push a function towards a long prefix of guard clauses, which is
  its own kind of unreadable and hides the fact that the function is doing too
  much.
- The guard clauses in upstream's good examples are single unbraced statements,
  which is the form that silently misbehaves when a second statement is added to
  the guard later.

### Neutral

- The rule is about nesting depth, not about function length; a short function
  with one level of nesting is fine and needs no change.
- Whether a guard clause returns, throws, or returns a failure value is a
  question about how errors are reported, and this rule settles only where the
  guard is written.

## Enforcement

- Flag a redundant `else`, that is, an `else` following a block from which
  control cannot fall through.
- Flag a function whose body is simply a conditional statement enclosing a
  block.

## Related

- [0009](0009-p-3-express-intent.md) — nesting that does not correspond to
  structure misstates the intent.
- [0024](0024-i-5-state-preconditions-if-any.md) — a guard clause is often an
  unstated precondition made executable.
