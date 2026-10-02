# 0216. ES.70: Prefer a switch-statement to an if-statement when there is a choice

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

There are three reasons. Readability. Efficiency: a `switch` compares against
constants and is usually better optimised than a series of tests in an
if-then-else chain. And a `switch` enables some heuristic consistency checking —
for example, have all the values of an enumeration been covered, and if not, is
there a `default`?

```cpp
void use(int n)
{
    switch (n) {   // good
    case 0:
        // ...
        break;
    case 7:
        // ...
        break;
    default:
        // ...
        break;
    }
}
```

rather than:

```cpp
void use2(int n)
{
    if (n == 0)   // bad: if-then-else chain comparing against a set of constants
        // ...
    else if (n == 7)
        // ...
}
```

The upstream if-then-else example is written without braces; braced or not, the
if-chain is the shape being argued against.

## Decision

Use a `switch` where the choice is between values of one expression against
constants.

- Convert an if-then-else chain that compares one expression against two or more
  constants into a `switch`.
- Switch over an enumeration without a `default` label where every enumerator is
  meant to be handled, so that adding an enumerator produces a compiler warning
  at every switch that needs updating.
- Add a `default` only where there genuinely is a catch-all behaviour, and where
  the value can legitimately be outside the enumeration — for example a value
  read from a file or received over a wire.
- Keep using `if` where the conditions are not comparisons of a single
  expression against constants, where they are ranges, or where they overlap.
- Brace every case body that declares a variable; case labels share one scope,
  so a declaration in one case is visible in the next.

## Rationale

The consistency checking is the reason that matters. An if-chain over an
enumeration is complete only by inspection, and stays complete only until
somebody adds an enumerator; a `switch` without a `default` gets a diagnostic at
exactly the moment the enumeration grows, at every site that needs attention.
That converts an open-ended maintenance obligation into a compiler error list.

Readability and code generation are real but secondary. A jump table beats a
chain of comparisons, but only measurably where the chain is long, and
[0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md)
applies to any claim that it matters here.

## Consequences

### Positive

- Adding an enumerator surfaces every place that must handle it, provided no
  `default` is present.
- The structure states plainly that one value is being dispatched on, which an
  if-chain only implies.
- The compiler is free to generate a jump table, which an if-chain generally
  does not permit.

### Negative

- `switch` works only on integral and enumeration types, so a dispatch on
  strings, on types, or on ranges cannot use it, and the rule quietly does not
  apply to a large share of real dispatches.
- Case labels share a single scope, so declaring an initialised variable inside
  a case is a compile error unless a block is added — the jump to a later case
  would cross its initialisation — a wart the if-chain does not have.
- Fallthrough between cases is implicit and silent; the form that makes
  exhaustiveness checkable also makes a missing `break` invisible.
- Adding a `default` to be safe defeats the exhaustiveness checking that is the
  rule's main argument, so the two benefits are in direct tension and the choice
  must be made case by case.

### Neutral

- Where the same handling applies to several values, stacked case labels are the
  idiomatic form and are not fallthrough in the problematic sense.

## Enforcement

Flag if-then-else chains that check against constants only.

## Related

- [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md) — the enumeration
  type that makes a switch's exhaustiveness checking worth having.
- [0140](0140-enum-2-use-enumerations-to-represent-sets-of-related-named-constants.md)
  — the constants a switch dispatches on should be an enumeration in the first
  place.
- [0009](0009-p-3-express-intent.md) — a switch says "dispatch on this value";
  an if-chain says only "test some conditions".
