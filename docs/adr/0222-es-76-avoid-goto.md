# 0222. ES.76: Avoid goto

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Readability and the avoidance of errors. There are better control structures for
humans; `goto` is for machine generated code.

There is one exception the guidelines allow: breaking out of a nested loop. In
that case, always jump forwards.

```cpp
for (int i = 0; i < imax; ++i)
    for (int j = 0; j < jmax; ++j) {
        if (a[i][j] > elem_max) goto finished;
        // ...
    }
finished:
// ...
```

There is a fair amount of use of the C `goto`-exit idiom:

```cpp
void f()
{
    // ...
        goto exit;
    // ...
        goto exit;
    // ...
exit:
    // ... common cleanup code ...
}
```

This is an ad-hoc simulation of destructors. Declare your resources with handles
whose destructors clean up. If for some reason you cannot handle all cleanup
with destructors for the variables used, `gsl::finally()` is a cleaner and more
reliable alternative to `goto exit`.

## Decision

Do not write `goto`, with a single narrow exception.

- Express cleanup with destructors: give every resource a handle whose
  destructor releases it, so there is no common exit block to jump to.
- Where no suitable handle exists, use a final-action object such as
  `gsl::finally()` rather than a label and a jump.
- Where a loop needs an early exit, prefer extracting the loop into a function
  and returning from it.
- If `goto` is used at all, restrict it to jumping forwards out of a nest of
  loops to the statement immediately following the nest, and to nothing else.
- Never jump backwards, and never jump into or across a scope containing
  initialised variables.

## Rationale

The `goto exit` idiom is not primarily a readability problem — it is a
correctness problem, because it is a hand-written destructor that runs only on
the paths the author remembered to route through the label. Every `return`,
every early exit and every future edit is a chance to leak. Destructors run on
all paths without being asked, which is why
[0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
replaces the idiom rather than tidying it.

The permitted exception is narrow for a reason: a forward jump out of a loop
nest to the statement after it is the one case where the language offers no
direct construct, and its control flow is still locally obvious.

## Consequences

### Positive

- Cleanup happens on every exit path, including exceptional ones, instead of
  only the paths that reach the label.
- Control flow can be read from the block structure alone, without scanning for
  labels.
- Removes the class of bug where a new early exit is added and the cleanup block
  is not updated.

### Negative

- Multi-level loop exit becomes more ceremonious: either a flag variable, an
  extracted function, or the permitted `goto` — none of which is as short as the
  jump.
- Wrapping a resource that has no handle costs a small type or a `finally`
  object, which is more code than a label for a one-off case.
- Porting or transcribing C code that uses the exit idiom means restructuring
  it, not translating it, which enlarges otherwise mechanical changes.
- The exception is a genuine hole: once `goto` is permitted at all, reviewers
  have to judge whether a given use is the sanctioned shape.

### Neutral

- Generated code is explicitly out of scope; the rule is about code humans read.
- The `goto exit` idiom comes from C, which has no destructors; the rule is a
  statement about which of C++'s facilities to reach for, not a criticism of the
  idiom in the language that needed it.

## Enforcement

- Flag `goto`.
- Better still, flag all `goto`s that do not jump from a nested loop to the
  statement immediately after the nest of loops.

## Related

- [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
  — the handles whose destructors replace the `goto exit` block.
- [0304](0304-e-19-use-a-final-action-object-to-express-cleanup-if-no-suitable-resource-handle-is-available.md)
  — `gsl::finally()`, the named alternative this rule points at.
- [0223](0223-es-77-minimize-the-use-of-break-and-continue-in-loops.md) — the
  companion rule on the structured jumps that remain.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the leak the exit idiom is
  trying and failing to prevent.
