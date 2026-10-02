# 0331. T.26: Prefer to define concepts in terms of use-patterns rather than simple syntax

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

A definition written in terms of use patterns is more readable and corresponds
directly to what a user has to write. Conversions are taken into account, and
there is no need to remember the names of all the type traits.

One might be tempted to define a concept `Equality` like this:

```cpp
template<typename T> concept Equality = has_equal<T> && has_not_equal<T>;
```

It would obviously be better and easier just to use the standard
`equality_comparable`, but as an example, if such a concept had to be defined,
prefer:

```cpp
template<typename T> concept Equality = requires(T a, T b) {
    { a == b } -> std::convertible_to<bool>;
    { a != b } -> std::convertible_to<bool>;
    // axiom { !(a == b) == (a != b) }
    // axiom { a = b; => a == b }  // => means "implies"
};
```

This is preferable to defining two meaningless concepts, `has_equal` and
`has_not_equal`, as helpers in the definition of `Equality`. They are meaningless
in the sense that the semantics of `has_equal` cannot be specified in isolation.

Upstream gives no enforcement for this rule.

## Decision

Define a concept by writing the expressions a user would write.

- Use a `requires`-expression containing the actual call, operator or
  initialisation the algorithm will perform, with a compound requirement stating
  what the result must convert to.
- Do not decompose a concept into single-property helper concepts or detection
  traits whose only purpose is to be conjoined back together.
- Let conversions do their work: the use pattern accepts a type whose `==`
  returns a proxy convertible to `bool`, where a trait asking for exactly `bool`
  would reject it.
- Attach the axioms to the use patterns, per
  [0327](0327-t-22-specify-axioms-for-concepts.md), since the pattern says what
  compiles and the axiom says what it must mean.

## Rationale

A use pattern is checked the way the code will actually be written, so it tests
the thing that matters rather than a trait's approximation of it — including the
implicit conversions, the overload resolution and the ADL that the real call
site will perform. The decomposed form fails twice over: it is more work to
write, and the pieces it creates are precisely the semantics-free
single-operation concepts that
[0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md) rules out.

## Consequences

### Positive

- The concept definition doubles as documentation of how the type will be used.
- Types that satisfy the requirement through a conversion or a proxy are
  accepted, as they would be at a real call site.
- No trait vocabulary to memorise, and no home-made helper concepts to maintain.
- The result constraint is stated where the expression is, so a wrong return type
  is caught at the concept rather than in the body.

### Negative

- A `requires`-expression tests only that the expression is well formed; it does
  not test that it does the right thing, so a use-pattern concept can be as
  semantically empty as a trait-based one.
- Long `requires`-expressions become hard to read, and when one fails the
  diagnostic points at an expression rather than at a named property, which for
  large concepts can be less informative than a failed trait.
- Writing the pattern requires exemplar objects (`requires(T a, T b)`), which is
  awkward for types that are not default constructible or where the pattern needs
  particular values; `std::declval` and helper aliases creep back in.
- Detection traits remain genuinely necessary in metaprogramming, so the rule is
  a preference for concept definitions rather than a ban on traits.

### Neutral

- The `Equality` example exists only to illustrate; the standard
  `std::equality_comparable` should be used instead, per
  [0322](0322-t-11-whenever-possible-use-standard-concepts.md).
- The axioms in the example are comments, which is all C++20 supports.

## Related

- [0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md) — why the
  `has_equal` helpers are rejected.
- [0327](0327-t-22-specify-axioms-for-concepts.md) — the semantics that accompany
  the use patterns.
- [0322](0322-t-11-whenever-possible-use-standard-concepts.md) — do not define
  this concept at all if the standard library has it.
- [0328](0328-t-23-differentiate-a-refined-concept-from-its-more-general-case-by-adding-new-use-patterns.md)
  — refinement is expressed by adding use patterns, so this is the form it takes.
