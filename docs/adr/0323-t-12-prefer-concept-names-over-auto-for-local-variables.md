# 0323. T.12: Prefer concept names over auto for local variables

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

`auto` is the weakest concept. A concept name conveys more meaning than `auto`
alone.

```cpp
vector<string> v{ "abc", "xyz" };
auto& x = v.front();        // bad
String auto& s = v.front(); // good (String is a GSL concept)
```

Both declarations deduce the same type. The difference is that the second says
what the author believed that type would be, and the compiler checks the belief
at the point it was written rather than at the first use that depends on it.

Upstream gives no enforcement for this rule; the Enforcement section is an
unfilled placeholder.

## Decision

When declaring a local variable with a deduced type, put a concept in front of
`auto` where a meaningful one applies.

- Write `std::integral auto n = ...` or `std::ranges::range auto r = ...` in
  preference to bare `auto` when the code that follows depends on that property.
- Keep the deduction: the point is to constrain the deduced type, not to go back
  to spelling it out, which
  [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md)
  argues against.
- Use bare `auto` where no concept expresses anything the reader does not already
  see — an iterator from a `begin()` call on the previous line, a lambda, the
  result of a factory whose name says the type.
- Do not invent a local concept purely to decorate a declaration; that is
  [0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md).

## Rationale

A deduced type is a claim the author is making silently, and a constrained `auto`
turns that claim into something the compiler verifies where it was made. The
failure this prevents is the one where a change elsewhere alters what a function
returns and the error appears several lines later, in an expression that looks
innocent, rather than at the declaration that was actually invalidated.

## Consequences

### Positive

- The reader learns something about the variable without having to trace the
  initialiser back to its source.
- A change to the initialiser's type is diagnosed at the declaration instead of
  at some later use.
- It uses the same vocabulary as the template constraints around it, so local
  code and generic interfaces read alike.

### Negative

- It is only as useful as the available concept: for many locals the only honest
  concept is "a type", and adding one anyway is noise.
- It lengthens declarations, partly undoing the brevity that motivates `auto`.
- A constrained `auto` can look like a conversion, and it is not — the deduced
  type is unchanged, only checked, which is a subtlety that misleads readers new
  to the syntax.
- Upstream offers no enforcement, so nothing prevents the rule from being applied
  in exactly the places where it adds least.

### Neutral

- This is a preference between two forms of deduction, not a position on
  deduction itself; [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md)
  still governs whether to deduce at all.
- The example's `String` came from the guidelines support library; the standard
  library equivalent would be a `std::convertible_to<std::string_view>`-style
  constraint.

## Related

- [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md) —
  the rule that puts `auto` there in the first place.
- [0321](0321-t-10-specify-concepts-for-all-template-arguments.md) — the same
  argument for template parameters, where it is much stronger.
- [0322](0322-t-11-whenever-possible-use-standard-concepts.md) — which concept
  name to use.
- [0009](0009-p-3-express-intent.md) — a constrained declaration states intent
  that a bare `auto` leaves implicit.
