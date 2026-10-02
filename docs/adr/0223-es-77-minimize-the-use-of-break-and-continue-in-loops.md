# 0223. ES.77: Minimize the use of break and continue in loops

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

In a non-trivial loop body, it is easy to overlook a `break` or a `continue`. A
`break` in a loop has a dramatically different meaning from a `break` in a
switch-statement, and you can have a switch-statement in a loop and a loop in a
switch-case:

```cpp
switch(x) {
case 1 :
    while (/* some condition */) {
        // ...
    break;
    } // Oops! break switch or break while intended?
case 2 :
    // ...
    break;
}
```

Often, a loop that requires a `break` is a good candidate for a function, in
which case the `break` becomes a `return`:

```cpp
//Original code: break inside loop
void use1()
{
    std::vector<T> vec = {/* initialized with some values */};
    T value;
    for (const T item : vec) {
        if (/* some condition*/) {
            value = item;
            break;
        }
    }
    /* then do something with value */
}
//BETTER: create a function and return inside loop
T search(const std::vector<T> &vec)
{
    for (const T &item : vec) {
        if (/* some condition*/) return item;
    }
    return T(); //default value
}
void use2()
{
    std::vector<T> vec = {/* initialized with some values */};
    T value = search(vec);
    /* then do something with value */
}
```

Often, a loop that uses `continue` can equivalently and as clearly be expressed
by an if-statement:

```cpp
for (int item : vec) {  // BAD
    if (item%2 == 0) continue;
    if (item == 5) continue;
    if (item > 10) continue;
    /* do something with item */
}
for (int item : vec) {  // GOOD
    if (item%2 != 0 && item != 5 && item <= 10) {
        /* do something with item */
    }
}
```

If you really do need to break out of a loop, a `break` is typically better than
alternatives such as modifying the loop variable or using a `goto`.

## Decision

Treat `break` and `continue` inside a loop as a signal that the loop is doing
too much, and prefer the restructured form.

- When a loop exists to find something, extract it into a function whose
  `return` replaces the `break`, and give that function a name.
- When `continue` is used as a filter, fold the conditions into a single
  positive `if` guarding the body, or use an algorithm or a filtered range.
- Do not put a `break` intended for a loop inside a switch-statement nested in
  that loop, or vice versa; restructure so that only one of the two is in scope.
- Where a `break` genuinely is the clearest expression, keep it — a `break` is
  still preferable to modifying the loop control variable or to `goto`.

## Rationale

The sharpest argument is the switch-inside-loop collision: `break` is one
keyword with two bindings, resolved by the innermost enclosing construct, and
neither the writer nor the reader gets any help from the compiler when the
intended target is the outer one. That is a defect the language cannot diagnose
because both readings compile.

The restructuring also pays elsewhere. Turning a search loop into a named
function gives the operation a name and a single exit, which is
[0439](0439-f-2-a-function-should-perform-a-single-logical-operation.md) applied
to loops.

## Consequences

### Positive

- Removes the ambiguity of `break` where a loop and a switch are nested.
- Search loops become named, reusable and independently testable functions.
- A loop body with no jumps out of it can be read top to bottom, so the exit
  conditions are all in the loop header.

### Negative

- The extracted-function form needs a sentinel or a default value for "not
  found", and `return T()` silently conflates "absent" with "default-constructed
  value" — the rewrite trades a control-flow problem for a representation
  problem.
- Folding several `continue` guards into one condition produces a long boolean
  expression, which runs against
  [0197](0197-es-40-avoid-complicated-expressions.md); with more than about
  three clauses the `continue` form may genuinely be clearer.
- Extracting a loop that captures several locals means passing them all, or
  building a struct for them, so the restructured version can be longer overall.
- The rule is a "minimize", not a prohibition, so it gives reviewers no crisp
  line and cannot be mechanically enforced.

### Neutral

- The guidelines give no enforcement for this rule, so it is a review
  convention rather than something a tool will catch.
- `break` inside a switch-statement is required, not discouraged; this record is
  about loops only.

## Related

- [0222](0222-es-76-avoid-goto.md) — the unstructured jump this rule still
  prefers `break` to.
- [0224](0224-es-78-dont-rely-on-implicit-fallthrough-in-switch-statements.md) —
  the other half of the `break` ambiguity, seen from the switch side.
- [0439](0439-f-2-a-function-should-perform-a-single-logical-operation.md) — the
  reason a search loop wants to be its own function.
- [0228](0228-es-86-avoid-modifying-loop-control-variables-inside-the-body-of-raw-for-loops.md)
  — the alternative this rule explicitly rates as worse than `break`.
