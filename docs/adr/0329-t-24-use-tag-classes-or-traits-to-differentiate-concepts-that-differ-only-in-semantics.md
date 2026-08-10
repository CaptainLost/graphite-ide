# 0329. T.24: Use tag classes or traits to differentiate concepts that differ only in semantics.

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Two concepts requiring the same syntax but having different semantics lead to
ambiguity unless the programmer differentiates them.

```cpp
template<typename I>    // iterator providing random access
// Note: random_access_iterator is defined in <iterator>
concept RA_iter = ...;

template<typename I>    // iterator providing random access to contiguous data
// Note: contiguous_iterator is defined in <iterator>
concept Contiguous_iter =
    RA_iter<I> && is_contiguous_v<I>;  // using is_contiguous trait
```

The programmer, in a library, must define `is_contiguous` appropriately.
Wrapping the tag class into a concept leads to a simpler expression of the same
idea:

```cpp
template<typename I> concept Contiguous = is_contiguous_v<I>;

template<typename I>
concept Contiguous_iter = RA_iter<I> && Contiguous<I>;
```

Traits can be trait classes or type traits, and they can be user-defined or come
from the standard library. Prefer the standard-library ones.

## Decision

When two concepts have identical syntactic requirements and different meanings,
add an explicit marker.

- Define a trait — a variable template or class template specialised by the
  author of each model — that asserts the semantic property, and conjoin it into
  the refined concept.
- Prefer an existing standard-library trait to a new one; define your own only
  where the standard library names nothing suitable.
- Wrap the trait in a named concept so that the constraint reads as a concept
  rather than as a raw `_v` variable.
- Require the model's author, not the concept's, to specialise the trait: opting
  in is a claim about a type, and only that type's author can make it.

## Rationale

Where [0328](0328-t-23-differentiate-a-refined-concept-from-its-more-general-case-by-adding-new-use-patterns.md)
fails — because the distinguishing property is a guarantee rather than an
expression — the only remaining option is an explicit declaration, and a trait is
that declaration in a form the constraint system can consume. The price is
honest: the opt-in is deliberate, so a type is contiguous because someone said so
and took responsibility for it, rather than because the compiler guessed from a
set of available operators.

## Consequences

### Positive

- Concepts that a compiler could not otherwise tell apart become distinguishable,
  and their overloads resolve.
- The semantic claim is recorded in code by the party who can actually make it.
- The trait can be specialised for a third-party type without modifying it.

### Negative

- It reintroduces exactly the registration burden that
  [0328](0328-t-23-differentiate-a-refined-concept-from-its-more-general-case-by-adding-new-use-patterns.md)
  praises concepts for avoiding: a type that qualifies but was never registered is
  silently excluded.
- Nothing verifies the claim. A trait specialised wrongly makes an algorithm take
  a fast path that is invalid for the type, with no diagnostic.
- Trait specialisations are scattered — typically far from both the concept and
  the algorithm — so the effective definition of the concept is spread across the
  program.
- The trait must be visible before the constraint is checked, so ordering and
  header dependencies start to matter in ways ordinary concepts avoid.

### Neutral

- C++20 supplies `std::contiguous_iterator` and its `iterator_concept` tag, so
  this specific example is now covered by the standard library.
- Using a trait here does not licence trait-based constraint machinery generally;
  it is the fallback for a case use patterns cannot express.

## Enforcement

- The compiler flags ambiguous use of identical concepts.
- Flag the definition of identical concepts.

## Related

- [0328](0328-t-23-differentiate-a-refined-concept-from-its-more-general-case-by-adding-new-use-patterns.md)
  — the preferred mechanism, and the rule that sends cases here.
- [0327](0327-t-22-specify-axioms-for-concepts.md) — the semantics the tag stands
  in for.
- [0322](0322-t-11-whenever-possible-use-standard-concepts.md) — prefer the
  standard library's traits and concepts.
- [0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md) — a tag-only
  concept is meaningful precisely because the tag names a semantic property.
