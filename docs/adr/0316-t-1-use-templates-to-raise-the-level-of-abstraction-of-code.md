# 0316. T.1: Use templates to raise the level of abstraction of code

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

The reasons given are generality, reuse, efficiency, and the encouragement of a
consistent definition of user-defined types.

Conceptually, the requirements below are wrong, because what is wanted of `T` is
more than the very low-level notions "can be incremented" or "can be added":

```cpp
template<typename T>
    requires Incrementable<T>
T sum1(vector<T>& v, T s)
{
    for (auto x : v) s += x;
    return s;
}

template<typename T>
    requires Simple_number<T>
T sum2(vector<T>& v, T s)
{
    for (auto x : v) s = s + x;
    return s;
}
```

Assuming that `Incrementable` does not support `+` and that `Simple_number` does
not support `+=`, the implementers of `sum1` and `sum2` have been
overconstrained, and in this case an opportunity for a generalisation has been
missed. The two functions compute the same thing and differ only in which
low-level operator each implementation happened to reach for.

```cpp
template<typename T>
    requires Arithmetic<T>
T sum(vector<T>& v, T s)
{
    for (auto x : v) s += x;
    return s;
}
```

Assuming that `Arithmetic` requires both `+` and `+=`, the user of `sum` is now
constrained to provide a complete arithmetic type. That is not a minimal
requirement, but it gives the implementer of the algorithm much-needed freedom,
and it ensures that any `Arithmetic` type can be used for a wide variety of
algorithms. For additional generality and reusability, a more general `Container`
or `Range` concept could be used instead of committing to one container type.

If a template is defined to require exactly the operations needed by a single
implementation of a single algorithm — requiring just `+=` rather than also `=`
and `+` — and only those, maintainers have been overconstrained. The aim is to
minimise requirements on template arguments, but the absolutely minimal
requirements of one implementation are rarely a meaningful concept.

Templates can express essentially everything, being Turing complete, but the aim
of generic programming as expressed using templates is to efficiently generalise
operations and algorithms over a set of types with similar semantic properties.

## Decision

Use templates to lift code to the level of the abstraction it is actually about,
not to the level of the operators one implementation happens to use.

- Parameterise an algorithm on a concept that names a coherent abstraction —
  `Arithmetic`, `Range`, `Sortable` — rather than on the individual operations
  the current body invokes.
- Do not define a one-off constraint whose only purpose is to admit exactly the
  operators used by today's implementation.
- Prefer parameterising over a range or container concept to committing to a
  single concrete container such as `vector`.
- Treat the low-level single-operation constraints as building blocks used inside
  concept definitions, not as the constraint a user-facing template advertises.

## Rationale

The requirement set of a template is part of its interface, and an interface
pinned to the body's current operators changes whenever the body does. Naming a
whole abstraction instead buys freedom for the implementer without meaningfully
costing the caller, because a type that supports `+` but not `+=` is a curiosity
rather than a case worth designing for. The sharpest point upstream makes is that
"the absolutely minimal requirements of an implementation is rarely a meaningful
concept" — minimality is not the goal, coherence is.

## Consequences

### Positive

- The requirement set stops churning every time the implementation is edited.
- Two algorithms over the same abstraction end up with the same constraints, so
  they compose and can be swapped.
- A type that satisfies a coherent concept works with a whole family of
  algorithms rather than with one function.
- Error messages name the abstraction the caller failed to provide instead of a
  missing operator deep in an instantiation.

### Negative

- Requiring more than the body needs genuinely rejects types that would have
  worked, and the caller has no recourse except to widen their type.
- It pulls directly against [0333](0333-t-41-require-only-essential-properties-in-a-templates-concepts.md);
  where "essential" ends and "gratuitous" begins is a judgement neither record
  can settle mechanically.
- Designing a coherent concept costs time up front, and a badly chosen one is
  worse than none because it is now part of a published interface.
- Raising the abstraction level tends to cost compile time and object size, since
  every distinct argument type instantiates fresh code.

### Neutral

- The examples are transcribed as written, including their abbreviated loop
  bodies and the pre-standard spelling of the concept names.
- The rule says nothing about when a template is warranted at all; that is
  [0317](0317-t-2-use-templates-to-express-algorithms-that-apply-to-many-argument-types.md).

## Enforcement

- Flag algorithms with overly simple requirements, such as direct use of specific
  operators without a concept.
- Do not flag the definition of the overly simple concepts themselves; they might
  simply be building blocks for more useful concepts.

## Related

- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — the interface-section rule that template parameters are documented with
  concepts; this record is about what the concept should be.
- [0321](0321-t-10-specify-concepts-for-all-template-arguments.md) — specify a
  concept for every template argument.
- [0333](0333-t-41-require-only-essential-properties-in-a-templates-concepts.md)
  — the counterweight: require only what is essential.
- [0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md) — why
  single-operation constraints are not concepts.
- [0563](0563-write-concrete-code-and-keep-templates-for-vocabulary-types.md) — amends this record: templates only for vocabulary types.
