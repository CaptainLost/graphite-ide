# 0322. T.11: Whenever possible use standard concepts

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Standard concepts — those provided by the guidelines support library and by the
ISO standard itself — save the work of thinking up one's own, are better thought
out than anything that can be managed in a hurry, and improve interoperability.
Unless a new generic library is being created, most of the concepts needed are
already defined by the standard library.

```cpp
template<typename T>
    // don't define this: sortable is in <iterator>
concept Ordered_container = Sequence<T> && Random_access<Iterator<T>> && Ordered<Value_type<T>>;

void sort(Ordered_container auto& s);
```

This `Ordered_container` is quite plausible, but it is very similar to the
`sortable` concept in the standard library. Is it better? Is it right? Does it
accurately reflect the standard's requirements for `sort`? It is better and
simpler just to use `sortable`:

```cpp
void sort(sortable auto& s);   // better
```

Designing a useful concept is challenging. The upstream note that the set of
standard concepts is still evolving dates from before C++20; since then the
standard concepts in `<concepts>`, `<iterator>` and `<ranges>` have been part of
the standard library.

## Decision

Reach for a standard concept before defining one.

- Search `<concepts>`, `<iterator>` and `<ranges>` for a concept that names what
  is meant, and use it unchanged.
- Build a new concept out of standard ones by conjunction where the standard
  library nearly fits, rather than restating their requirements by hand.
- Define an entirely new concept only for a domain notion the standard library
  has no name for, and then hold it to
  [0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md) and
  [0326](0326-t-21-require-a-complete-set-of-operations-for-a-concept.md).
- Do not define a private synonym for a standard concept, however slight the
  wording difference.

## Rationale

A home-made concept is not merely redundant: it is a second, unverified statement
of requirements that standard algorithms will not agree with, so a type can
satisfy the local concept and still fail against `std::sort`. The three questions
upstream asks about `Ordered_container` — is it better, is it right, does it
match what `sort` actually needs — have no cheap answer, and answering them
wrongly is invisible until a caller's type is rejected or, worse, accepted.

## Consequences

### Positive

- Types written against a standard concept interoperate with the standard library
  and with any other code that uses the same vocabulary.
- The semantic requirements come already specified, including the parts that
  cannot be checked syntactically.
- Less code to write, review and maintain, and no drift when the requirements of
  an algorithm are refined.

### Negative

- Standard concepts are often stricter than the code needs — `std::sortable`
  implies a good deal about the iterator — so using one can reject a type that
  the algorithm would have handled.
- Their names are chosen for the standard library's domain, not the application's,
  so a constraint written purely in standard vocabulary can read as less
  meaningful than a well-named domain concept.
- Diagnostics from an unsatisfied standard concept are frequently deep, since
  they decompose into layers of other standard concepts.
- Upstream itself calls enforcement "hard", so this rule is largely a review
  convention.

### Neutral

- The example's `Sequence`, `Random_access` and `Ordered` were library concepts
  of the pre-standard era; the point survives their names.
- Deciding that no standard concept fits is legitimate; the rule asks that the
  search happen, not that the answer always be yes.

## Enforcement

Hard.

- Look for unconstrained arguments, for templates that use unusual or
  non-standard concepts, and for templates using home-brew concepts without
  axioms.
- Develop a concept-discovery tool.

## Related

- [0321](0321-t-10-specify-concepts-for-all-template-arguments.md) — every
  argument needs a concept; this record says which one.
- [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md)
  — the same preference applied to code rather than to constraints.
- [0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md) — the bar a
  home-made concept must clear.
- [0327](0327-t-22-specify-axioms-for-concepts.md) — the axioms a home-brew
  concept is expected to state.
