# 0220. ES.74: Prefer to declare a loop variable in the initializer part of a for-statement

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Upstream, this rule has no body of its own: it consists of the title and a
cross-reference to ES.6, "Declare names in for-statement initializers and
conditions to limit scope", which is recorded here as
[0176](0176-es-6-declare-names-in-for-statement-initializers-and-conditions-to-limit-scope.md).
This record therefore restates the rule in its own terms and defers to that one
for the reasoning and the examples.

The substance is scope. A loop variable declared in the initialiser part of a
`for` statement exists from that point to the end of the loop and no longer:

```cpp
for (int i = 0; i < n; ++i) {   // good: i is scoped to the loop
    // ...
}
```

Declared outside, it survives the loop, and every later line in the enclosing
scope can read it, assign to it, or reuse it for something unrelated:

```cpp
int i = 0;                      // bad: i outlives the loop
for (; i < n; ++i) {
    // ...
}
// ... i is still here, and still whatever the loop left it as
```

The same applies to `while` and `if` conditions, where C++ allows a declaration,
and to the init-statement forms of `if`, `switch` and range-for.

## Decision

Declare each loop variable where the loop begins.

- Declare the loop variable in the initialiser part of the `for` statement, not
  in the enclosing scope.
- Where the loop is a range-for, the loop variable is already scoped to the
  loop; choose its form — `const auto&`, `auto&`, or a value — per
  [0217](0217-es-71-prefer-a-range-for-statement-to-a-for-statement-when-there-is-a-choice.md).
- Use the init-statement forms of `if` and `switch`, and the C++20
  init-statement form of range-for, to give a value the same treatment when it
  is needed only for one statement.
- Do not declare a variable before the loop merely so that its value can be
  inspected in a debugger, or because it might be wanted later; if it really is
  wanted after the loop, that is a different variable with a different purpose,
  and it should be named accordingly.

## Rationale

A variable that outlives its loop has an ambiguous value afterwards: it holds
whatever the loop's exit condition left, which depends on how the loop ended.
Confining it to the initialiser removes the question, and lets the same short
name — `i` — be used by the next loop without either loop being affected by the
other.

The rule is also what makes the `for`-versus-`while` preference of
[0218](0218-es-72-prefer-a-for-statement-to-a-while-statement-when-there-is-an-obvious-loop-variable.md)
worth having. Moving the initialisation into the header is only an improvement
if the declaration moves with it.

## Consequences

### Positive

- The loop variable cannot be read after the loop, where its value is a
  by-product rather than a result.
- Short names can be reused loop by loop without collision, keeping the
  convention that a loop index is called `i`.
- Initialisation and use sit together, satisfying
  [0184](0184-es-21-dont-introduce-a-variable-or-constant-before-you-need-to-use-it.md)
  in the same stroke.

### Negative

- It does not work when the value is needed after the loop — a search that
  reports the index it stopped at, for instance. Those loops must declare the
  variable outside, and the rule offers nothing for them.
- A loop with two control variables of different types cannot declare both in
  the initialiser, so it needs a struct, a pair, or a declaration outside the
  loop; the rule's shape assumes a single index.
- Upstream this rule has no content of its own beyond a cross-reference, so
  transcribing it produces a record that overlaps
  [0176](0176-es-6-declare-names-in-for-statement-initializers-and-conditions-to-limit-scope.md)
  almost entirely. Two records saying nearly the same thing is a maintenance
  cost in itself.
- There is no upstream enforcement to inherit, so this is a review convention
  rather than something a tool will catch.

### Neutral

- Where the loop is written as a range-for or replaced by an algorithm, the
  question does not arise at all.

## Related

- [0176](0176-es-6-declare-names-in-for-statement-initializers-and-conditions-to-limit-scope.md)
  — the rule this one refers to, and where the reasoning lives.
- [0175](0175-es-5-keep-scopes-small.md) — the general principle.
- [0184](0184-es-21-dont-introduce-a-variable-or-constant-before-you-need-to-use-it.md)
  — the timing half of the same idea.
- [0218](0218-es-72-prefer-a-for-statement-to-a-while-statement-when-there-is-an-obvious-loop-variable.md)
  — the loop form that has an initialiser part to declare into.
