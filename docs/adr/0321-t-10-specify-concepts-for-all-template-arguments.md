# 0321. T.10: Specify concepts for all template arguments

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Correctness and readability. The assumed meaning — syntax and semantics — of a
template argument is fundamental to the interface of a template. A concept
dramatically improves both the documentation and the error handling of the
template, and specifying concepts for template arguments is a powerful design
tool in its own right.

```cpp
template<typename Iter, typename Val>
    requires input_iterator<Iter>
             && equality_comparable_with<iter_value_t<Iter>, Val>
Iter find(Iter b, Iter e, Val v)
{
    // ...
}
```

or equivalently and more succinctly:

```cpp
template<input_iterator Iter, typename Val>
    requires equality_comparable_with<iter_value_t<Iter>, Val>
Iter find(Iter b, Iter e, Val v)
{
    // ...
}
```

Plain `typename`, or `auto`, is the least constraining concept. It should be used
only rarely, when nothing more than "it is a type" can be assumed. That is
typically needed only in template metaprogramming code that manipulates pure
expression trees, postponing type checking.

Concepts are a language feature rather than an aspiration: C++20 makes
`requires` clauses and constrained template parameters part of the language, so
the declarations above compile as written.

## Decision

Constrain every template type parameter.

- Give each type parameter a concept, either as a constrained parameter
  (`template<input_iterator Iter>`) or in a `requires` clause.
- Express relationships between parameters — that a value is comparable with an
  iterator's value type, for instance — as a constraint too, not as a comment.
- Leave a parameter as bare `typename` only where genuinely nothing but
  "it is a type" is assumed, and say why in a comment when that happens.
- Where a suitable concept does not exist, use a standard one if there is a near
  fit ([0322](0322-t-11-whenever-possible-use-standard-concepts.md)) before
  defining a new one.

## Rationale

The constraint set is the template's interface; leaving it unwritten does not
make the requirements disappear, it makes them discoverable only by instantiating
and reading the failure. This is
[0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) applied to
generic code — the check moves from "does it happen to compile at the call site"
to "does the declaration say it is allowed" — and the diagnostic moves with it,
from the middle of the body to the point of use.

## Consequences

### Positive

- A caller can read what a template requires without reading its body.
- Errors are reported against the declaration, naming the unsatisfied concept,
  instead of unwinding an instantiation stack.
- Constrained overloads become orderable by subsumption, which removes a class of
  ambiguity and the need for tag tricks.
- The act of naming the constraint often reveals that the design wanted a
  different abstraction.

### Negative

- Every template declaration gets longer, and multi-parameter relationships in a
  `requires` clause can be harder to read than the body they guard.
- A constraint that is too tight rejects working types, and tightening or
  loosening it later is a breaking interface change.
- Getting good diagnostics still depends on the concept being well factored; a
  single monolithic `requires` clause fails as opaquely as no constraint at all.
- Constraints are checked on the declaration, not the body, so an unconstrained
  operation used in the body still fails late — which
  [0333](0333-t-41-require-only-essential-properties-in-a-templates-concepts.md)
  accepts deliberately.

### Neutral

- Non-type template parameters already have types and are outside this rule.
- The choice between constrained-parameter syntax and a `requires` clause is
  settled by [0324](0324-t-13-prefer-the-shorthand-notation-for-simple-single-type-argument-concepts.md).

## Enforcement

- Flag template type arguments without concepts.

## Related

- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — the interface-section statement of the same requirement.
- [0322](0322-t-11-whenever-possible-use-standard-concepts.md) — which concept to
  reach for.
- [0324](0324-t-13-prefer-the-shorthand-notation-for-simple-single-type-argument-concepts.md)
  — how to write it.
- [0339](0339-t-48-if-your-compiler-does-not-support-concepts-fake-them-with-enable-if.md)
  — what this rule degrades to without language support.
