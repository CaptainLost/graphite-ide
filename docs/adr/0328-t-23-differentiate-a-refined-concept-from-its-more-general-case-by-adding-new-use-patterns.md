# 0328. T.23: Differentiate a refined concept from its more general case by adding new use patterns.

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Otherwise the two concepts cannot be distinguished automatically by the compiler.

```cpp
template<typename I>
// Note: input_iterator is defined in <iterator>
concept Input_iter = requires(I iter) { ++iter; };

template<typename I>
// Note: forward_iterator is defined in <iterator>
concept Fwd_iter = Input_iter<I> && requires(I iter) { iter++; };
```

The compiler can determine refinement from the sets of required operations — here
the suffix `++`. That decreases the burden on the implementers of these types,
since they need no special declaration to hook into the concept. If two concepts
have exactly the same requirements they are logically equivalent, and there is no
refinement.

## Decision

Make a refinement visible in what it requires.

- Define the refined concept as the general one conjoined with at least one
  additional use pattern that only models of the refinement support.
- Do not distinguish two concepts by name, by comment or by intent alone; the
  compiler orders overloads by requirement sets, and nothing else is visible to
  it.
- Do not require a type to opt in with a declaration or a base class when an
  extra required expression would do.
- Where the refinement genuinely adds no expression and differs only in meaning,
  it is not this rule's case; use a trait or tag as
  [0329](0329-t-24-use-tag-classes-or-traits-to-differentiate-concepts-that-differ-only-in-semantics.md)
  describes.

## Rationale

Subsumption is the mechanism that makes constrained overloads resolve without tag
dispatch, and it works only on the syntactic requirements a concept states — so
two concepts that require the same things are the same concept as far as overload
resolution is concerned, no matter what they are called. The corollary upstream
draws is the valuable one: because refinement is deduced from requirements, a
type becomes a model of the refined concept simply by supporting the extra
operation, with no registration step, which is what keeps concepts open to types
their author never saw.

## Consequences

### Positive

- Overloads on the general and the refined concept resolve unambiguously, with
  the refinement preferred.
- Types opt into a refinement by having the operations, not by declaring anything.
- The difference between two related concepts is readable directly from their
  definitions.

### Negative

- It forces the distinguishing property to be expressible as an expression, and
  some real refinements — the multi-pass guarantee that actually separates a
  forward iterator from an input iterator — are not, so the syntactic marker ends
  up being a proxy for the semantic property.
- The proxy can be satisfied accidentally: a type providing suffix `++` with
  input-iterator semantics silently claims to be a forward iterator, and nothing
  detects it.
- Subsumption reasoning depends on how the constraints were written; two concepts
  with equivalent but differently spelled requirements are not ordered, so the
  refinement must literally name the general concept.
- Enforcement can only find concepts with identical requirement sets, not
  refinements that are missing a marker.

### Neutral

- The example's concepts are the standard library's `input_iterator` and
  `forward_iterator` under different names; per
  [0322](0322-t-11-whenever-possible-use-standard-concepts.md) the standard ones
  would be used in preference.
- The rule constrains concept definition, not the algorithms that consume them.

## Enforcement

- Flag a concept that has exactly the same requirements as another
  already-declared concept, where neither is more refined. To disambiguate them,
  see [0329](0329-t-24-use-tag-classes-or-traits-to-differentiate-concepts-that-differ-only-in-semantics.md).

## Related

- [0329](0329-t-24-use-tag-classes-or-traits-to-differentiate-concepts-that-differ-only-in-semantics.md)
  — the remedy when the difference is semantic only.
- [0327](0327-t-22-specify-axioms-for-concepts.md) — where the semantic part of a
  refinement is written down.
- [0326](0326-t-21-require-a-complete-set-of-operations-for-a-concept.md) — the
  requirement sets being compared should each be coherent.
- [0330](0330-t-25-avoid-complementary-constraints.md) — the other way people try
  to separate overloads, and should not.
