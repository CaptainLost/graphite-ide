# 0314. Con.4: Use const to define objects with values that do not change after construction

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Con (Constants and immutability), transcribed 2026-08-10

## Context

The reason given is a short one: prevent surprises from unexpectedly changed
object values.

```cpp
void f()
{
    int x = 7;
    const int y = 9;
    for (;;) {
        // ...
    }
    // ...
}
```

As `x` is not `const`, we must assume that it is modified somewhere in the loop.
The loop body is elided in the example, and that is the point — the reader of
`f()` cannot know what `x` is worth at the bottom of the function without
reading everything in between, while `y` is settled at its declaration and stays
settled.

Where this rule differs from
[0311](0311-con-1-by-default-make-objects-immutable.md) is emphasis rather than
substance. Con.1 states the default and argues from ease of reasoning; this rule
names the criterion that decides an individual declaration, which is whether the
value changes after the object is constructed. That criterion also reaches past
locals: a data member fixed by the constructor, a namespace-scope table, a
`static` lookup built once, and a captured value in a long-lived object are all
objects with values that do not change after construction.

## Decision

Write `const` on the definition of any object whose value is settled once it has
been constructed.

- Decide per declaration by asking whether the object is assigned after its
  initialiser runs; if it is not, it is `const`.
- Initialise at the point of definition so that the question can be answered
  there, rather than declaring first and assigning later.
- Where the value needs several statements to compute, compute it in an
  immediately invoked lambda and bind the result to a `const`, rather than
  leaving the object mutable for the sake of its construction.
- Apply the criterion to `static` and namespace-scope objects, where an
  unnecessary mutable object is also a threading and initialisation-order
  problem.
- For a data member fixed at construction, weigh the `const` against
  [0050](0050-c-12-dont-make-data-members-const-or-references.md), which prefers
  a non-`const` private member with no mutating interface to a `const` member
  that makes the class non-assignable.

## Rationale

The example is about what a non-`const` declaration costs a reader, not about
what a `const` one saves the writer. Every unmarked variable in scope is an open
question that the loop below might answer, and answering it means reading code
that has nothing to do with what the reader came for; `const` closes the
question at the declaration and has the compiler keep it closed.

The criterion is deliberately mechanical — assigned after initialisation, or not
— because a judgement call at every declaration is the thing that stops the
default from being applied consistently, and a default applied inconsistently
tells a reader nothing.

## Consequences

### Positive

- The mutable objects in a function are exactly the ones that are not marked, so
  the state a reader must track is visible without reading the bodies.
- A later edit that starts assigning to a settled value fails to compile, which
  turns a silent behavioural change into a decision someone has to make.
- Applying the criterion pushes declarations towards their initialisers, which
  shortens the lifetime of the uninitialised state.

### Negative

- This rule and [0311](0311-con-1-by-default-make-objects-immutable.md) overlap
  almost entirely, so two records govern the same declaration and can be cited
  against each other in a way that adds discussion without adding information.
- `const` on a local defeats move semantics: returning or passing a `const`
  local copies where a non-`const` one would have moved, and no diagnostic
  mentions it.
- "Does not change after construction" is a claim about the future. Values
  acquire mutability as requirements change, and the resulting `const`-removal
  churn shows up in diffs as noise around the actual change.
- On data members the rule collides with assignability, so the honest form of it
  for members is encapsulation rather than the keyword.

### Neutral

- The rule says nothing about how the value is computed, only that it is fixed
  once it has been.
- Where the initialiser is a constant expression, the stronger keyword is
  `constexpr`, which is
  [0315](0315-con-5-use-constexpr-for-values-that-can-be-computed-at-compile-time.md)
  and not this record.

## Enforcement

Flag unmodified non-`const` variables. The check is one-sided: a tool can see
that an object was never assigned, but not whether the author intended it to be
assignable, so each report is a suggestion rather than a defect.

## Related

- [0311](0311-con-1-by-default-make-objects-immutable.md) — the default this rule
  applies at an individual declaration.
- [0188](0188-es-25-declare-an-object-const-or-constexpr-unless-you-want-to-modify-its-value-later-on.md)
  — the same criterion stated among the rules for expressions and statements.
- [0050](0050-c-12-dont-make-data-members-const-or-references.md) — why the rule
  stops short of data members.
- [0315](0315-con-5-use-constexpr-for-values-that-can-be-computed-at-compile-time.md)
  — the stronger claim available when the value is known during translation.
