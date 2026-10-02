# 0387. SL.1: Use libraries wherever possible

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

Using only the bare language, every task is tedious, in any language. Using a
suitable library, any task can be reasonably simple. The standard library has
grown steadily over the years, and its description in the standard is now larger
than that of the language features themselves.

The reason upstream gives for the rule is short and entirely practical: save
time, do not re-invent the wheel, do not replicate the work of others, benefit
from other people's work when they make improvements, and help other people when
you make improvements.

That last clause is the part usually missed. A library is not only code you did
not have to write; it is code whose future maintenance is shared. Every bug
fixed upstream arrives for free, and every fix contributed upstream is repaid by
everyone else's use of it. A hand-rolled equivalent inside one program has none
of that leverage.

## Decision

Reach for an existing library before writing new code for a problem that
libraries already solve.

- Before implementing a container, an algorithm, a string operation, a
  date/time calculation, a parser, or an allocator, check whether the standard
  library or an already-vendored dependency provides it.
- Prefer improving or extending an existing library over forking a private copy
  of its behaviour into your own code.
- When a library is nearly right, adapt it behind a thin wrapper rather than
  reimplementing it; the wrapper is the place where local requirements live.
- Where a genuinely local implementation is required, record why the library
  option did not fit, so the decision can be re-judged when the library changes.

## Rationale

The argument that decides this rule is not effort saved once, it is defect rate
over time. A widely used library has been run against inputs no single project
will ever generate, and its bugs are found by other people's tests. Code written
locally is exercised only by the program that contains it.

This is the section-level statement of
[0019](0019-p-13-use-support-libraries-as-appropriate.md); everything else in
the SL section is a specific instance of it.

## Consequences

### Positive

- Less code to write, review, test and maintain locally.
- Improvements and fixes made upstream arrive without local work.
- Library interfaces are usually already documented, so intent is legible to a
  reader who has never seen the surrounding code.
- Effort is redirected to what is actually specific to the product.

### Negative

- Every library is a dependency: a build cost, a licence, a version to track,
  and a supply-chain surface. Adding one is never free.
- A library's abstraction may not match the problem, and bending the problem to
  fit the library produces worse code than writing thirty lines by hand.
- Debugging through library code is harder than debugging your own, especially
  in heavily templated code where diagnostics are poor.
- The rule gives no threshold, so "wherever possible" can be read as licence to
  pull in a large dependency for a trivial need.

### Neutral

- The rule says nothing about which library to prefer; that is
  [0388](0388-sl-2-prefer-the-standard-library-to-other-libraries.md).
- It also says nothing about how a library is obtained or built; the mechanics
  of acquiring one are a separate question from whether to.

## Related

- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — the philosophy
  rule this whole section implements.
- [0388](0388-sl-2-prefer-the-standard-library-to-other-libraries.md) — which
  library to reach for first.
- [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md)
  — the same rule stated again in the expressions and statements section.
- [0173](0173-es-2-prefer-suitable-abstractions-to-direct-use-of-language-features.md)
  — the same instinct one level down, at the abstraction rather than the
  library.
