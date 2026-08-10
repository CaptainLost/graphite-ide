# 0212. ES.62: Don't compare pointers into different arrays

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

The result of comparing pointers into different arrays is not something the
language defines.

```cpp
void f()
{
    int a1[7];
    int a2[9];

    if (&a1[5] < &a2[7]) {}       // bad: undefined
    if (0 < &a1[5] - &a2[7]) {}   // bad: undefined
}
```

The relational operators on pointers are specified only for pointers into the
same array or object — including the one-past-the-end position — and so is
pointer subtraction. Two arrays are unrelated objects, so neither their ordering
nor the distance between them is a thing the language gives a value for, however
obvious the answer may look on a machine with a flat address space.

The two halves of the example fail in different ways, and upstream calls both
undefined. Subtracting pointers into different objects is undefined behaviour
outright, so the program containing it has no defined meaning at all. The
relational comparison is weaker: its result is unspecified, which means the
implementation may answer however it likes, need not document the choice, and
need not answer the same way twice. Neither is a question to build code on.

Equality comparison is a separate matter and is well defined for any two object
pointers; it is the relational operators and subtraction that this rule is
about. As with several rules in this part of the guidelines, the upstream
example is noted as having many other problems besides the one being
illustrated.

## Decision

Compare or subtract pointers only within a single array or object.

- Do not use `<`, `<=`, `>` or `>=` on pointers that may point into different
  objects.
- Do not subtract pointers into different objects, and do not compare the result
  of such a subtraction against anything.
- Where a total order over unrelated pointers is genuinely needed — as a key in
  a `std::map`, or to canonicalise a lock acquisition order — use
  `std::less<T*>`, which is specified to give a strict total order across all
  pointers of a type.
- Where the intent is "is this element inside this range", express it with the
  range rather than with pointer arithmetic: hold a `std::span` and compare
  indices, per
  [0199](0199-es-42-keep-use-of-pointers-simple-and-straightforward.md).
- Do not use pointer ordering as a proxy for object identity; equality
  comparison answers that question and is well defined.

## Rationale

This rule is easy to dismiss because the forbidden code produces sensible
answers on every mainstream platform, and that is precisely why it is worth
recording. The prohibition is not about the hardware; it is about what the
optimiser is permitted to assume. A comparison whose result the standard
declines to pin down is one the compiler may fold to whatever constant suits it,
and a range check built on it may simply cease to exist.

The constructive part is that the language already provides the escape hatch.
`std::less` on pointers is defined to work exactly where the built-in operator
is not, so code that genuinely needs to order unrelated pointers has a correct
way to say so, and the difference between the two spellings tells the reader
which situation this is.

## Consequences

### Positive

- Removes a class of comparison whose result an optimiser is free to invent.
- Directs the cases that really need pointer ordering to `std::less`, where the
  intent is visible.
- Encourages expressing containment questions against a range type, which is
  checkable, rather than against raw addresses, which are not.

### Negative

- It is close to unenforceable. The upstream rule offers no enforcement at all,
  and no analyser can generally establish that two pointers came from different
  objects when they arrived as function parameters.
- The forbidden code works, consistently, on every platform anyone is likely to
  test on, so the rule has no empirical support from the developer's own
  experience — it will look like pedantry until the day it does not.
- `std::less` is a heavier and less readable spelling than `<`, and it is easy
  to forget that it exists; the rule as written upstream does not mention it at
  all.
- Pointer provenance is not visible in a pointer's type, so the rule cannot be
  checked by inspection at the point of comparison either — only by tracing
  where each pointer came from.

### Neutral

- Comparing a pointer against `nullptr`, or two pointers for equality, is
  unaffected.
- One-past-the-end pointers into the same array are within the defined region
  and may be compared normally.

## Related

- [0199](0199-es-42-keep-use-of-pointers-simple-and-straightforward.md) — pointer
  arithmetic is the activity that produces these comparisons.
- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — once the extent
  travels with the data, the question does not arise.
- [0215](0215-es-65-dont-dereference-an-invalid-pointer.md) — the other
  undefined operation on a pointer whose provenance is not known.
- [0008](0008-p-2-write-in-iso-standard-c.md) — relying on what the hardware
  happens to do is exactly what that record rules out.
