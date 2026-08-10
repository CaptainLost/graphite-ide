# 0317. T.2: Use templates to express algorithms that apply to many argument types

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

The reasons are generality, minimising the amount of source code,
interoperability, and reuse. This is the foundation of the standard library: a
single `find` algorithm works with any kind of input range.

```cpp
template<typename Iter, typename Val>
    // requires Input_iterator<Iter>
    //       && Equality_comparable<Value_type<Iter>, Val>
Iter find(Iter b, Iter e, Val v)
{
    // ...
}
```

The rule comes with its own brake: do not use a template unless there is a
realistic need for more than one template argument type. Do not overabstract.

Upstream offers no mechanical check for either half of that, observing only that
the judgement is hard and probably needs a human.

## Decision

Write an algorithm once, as a template, when it genuinely applies to more than
one argument type.

- When the same logic is about to be written a second time for a second type,
  make it a template rather than copying it.
- Parameterise on the types that actually vary; leave everything else concrete.
- Do not template a function that has exactly one plausible instantiation. A
  concrete function is cheaper to read, to compile, and to debug.
- Constrain the parameters with concepts as
  [0321](0321-t-10-specify-concepts-for-all-template-arguments.md) requires, so
  that "applies to many types" is stated rather than implied.

## Rationale

A template earns its cost when the alternative is duplication that must then be
kept in step; one definition cannot drift from itself. The brake matters as much
as the rule, because a template with a single instantiation pays every cost of
genericity — slower builds, worse diagnostics, a body that cannot be read without
imagining substitutions — and collects none of the benefit.

## Consequences

### Positive

- One definition to fix when the algorithm is wrong, rather than one per type.
- Types that were never anticipated can use the algorithm without it being
  touched.
- Duplication that would otherwise drift is removed, which is
  [0174](0174-es-3-dont-repeat-yourself-avoid-redundant-code.md) applied to
  algorithms.

### Negative

- "A realistic need for more than one type" is a prediction, and predicting it
  wrongly in either direction costs: too early and the abstraction is speculative
  and probably wrong, too late and there are already two copies.
- Errors surface at instantiation, in the caller's context rather than the
  author's, which is a worse place to read them.
- Every instantiation is separate code; a widely instantiated template inflates
  build time and binary size in ways a concrete function does not.
- The rule is unenforceable — upstream says so outright — so it lives or dies on
  review.

### Neutral

- The example still writes its requirements as comments; in C++20 they would be
  written as real constraints.
- Whether to reach for a standard algorithm instead of writing one at all is
  [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md).

## Enforcement

- Not mechanically checkable. Upstream offers no automated test and notes that
  deciding whether a template is warranted probably needs a human.

## Related

- [0316](0316-t-1-use-templates-to-raise-the-level-of-abstraction-of-code.md) —
  the abstraction level such a template should be written at.
- [0318](0318-t-3-use-templates-to-express-containers-and-ranges.md) — the
  container-and-range half of the same idea.
- [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md)
  — the generic algorithm you need may already exist.
- [0174](0174-es-3-dont-repeat-yourself-avoid-redundant-code.md) — duplication is
  what this rule removes.
