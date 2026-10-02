# 0327. T.22: Specify axioms for concepts

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

A meaningful, useful concept has a semantic meaning. Expressing those semantics
informally, semi-formally or formally makes the concept comprehensible to
readers, and the effort of expressing it catches conceptual errors. Specifying
semantics is a powerful design tool.

```cpp
template<typename T>
    // The operators +, -, *, and / for a number are assumed to follow the usual mathematical rules
    // axiom(T a, T b) { a + b == b + a; a - a == 0; a * (b + c) == a * b + a * c; /*...*/ }
    concept Number = requires(T a, T b) {
        {a + b} -> convertible_to<T>;
        {a - b} -> convertible_to<T>;
        {a * b} -> convertible_to<T>;
        {a / b} -> convertible_to<T>;
    };
```

This is an axiom in the mathematical sense: something that can be assumed without
proof. In general axioms are not provable, and when they are, the proof is often
beyond the capability of a compiler. An axiom might not be general, but the
template writer can assume it holds for all inputs actually used, similarly to a
precondition.

In this context axioms are Boolean expressions. C++ does not support axioms, so
they have to be written as comments; if language support ever arrives, the `//`
in front of the axiom can be removed.

There is an exception. Early versions of a new concept still under development
will often just define simple sets of constraints without well-specified
semantics. Finding good semantics can take effort and time, and an incomplete set
of constraints can still be very useful:

```cpp
// balancer for a generic binary tree
template<typename Node> concept Balancer = requires(Node* p) {
    add_fixup(p);
    touch(p);
    detach(p);
};
```

A `Balancer` must supply at least these operations on a tree `Node`, but the
detailed semantics are not yet specifiable, because a new kind of balanced tree
might require more operations and the precise general semantics for all nodes are
hard to pin down early in a design. Such a concept still allows some checking
during initial experimentation, but it should not be assumed to be stable: each
new use case might require it to be improved.

## Decision

Write the semantics a concept assumes next to the concept.

- Attach a comment to each concept definition stating, as Boolean expressions
  where possible, the laws its models must obey — commutativity, the relationship
  between `==` and `!=`, what an operation leaves behind.
- Treat those laws the way a precondition is treated: an implementation may
  assume them, and a type that violates them is not a model, however well it
  compiles.
- Where the semantics are not yet known, say so explicitly and mark the concept
  unstable rather than pretending the syntactic constraints are the whole story.
- Do not use the absence of language support as a reason to omit the axioms; a
  comment in the agreed `axiom(...)` shape is what the rule asks for.

## Rationale

Every generic algorithm already assumes semantics — a sort assumes its comparison
is a strict weak ordering, a sum assumes addition is associative enough to
reorder — and the only question is whether those assumptions are written down or
discovered by a user whose type breaks them. Upstream's strongest claim is not
about documentation at all: the effort of stating the semantics catches
conceptual errors, which is how `Addable` gets recognised as not being a concept.

## Consequences

### Positive

- Implementers know what they may assume, and authors of models know what they
  must uphold.
- Writing the axioms exposes incoherent concepts before they are published.
- Semantic requirements become reviewable artefacts rather than folklore.
- Two concepts with identical syntax can be told apart by their stated laws, even
  where the compiler cannot.

### Negative

- The axioms are comments: nothing checks them, nothing updates them when the
  concept changes, and a stale axiom is a confident lie.
- Stating semantics well is genuinely hard and can stall a design, which is why
  the rule needs an explicit escape hatch for concepts still under development.
- The escape hatch is broad enough to swallow the rule — any concept can be
  called "still under development".
- Enforcement amounts to grepping for the word "axiom", which detects the
  presence of a comment and nothing about its truth.

### Neutral

- Axioms are assumed for the inputs actually used, not proved universally, so a
  type that satisfies them over its real domain is a model.
- The tension between this rule and
  [0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md) is deliberate:
  a syntax-only concept is discouraged in general and permitted while a design is
  still being explored.

## Enforcement

- Look for the word "axiom" in concept definition comments.

## Related

- [0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md) — the axioms
  are what makes a concept meaningful.
- [0326](0326-t-21-require-a-complete-set-of-operations-for-a-concept.md) —
  coherent operation sets are what the axioms relate.
- [0024](0024-i-5-state-preconditions-if-any.md) — an axiom is assumed the way a
  precondition is.
- [0329](0329-t-24-use-tag-classes-or-traits-to-differentiate-concepts-that-differ-only-in-semantics.md)
  — what to do when only the axioms differ.
