# 0028. I.9: If an interface is a template, document its parameters using concepts

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Concepts make a template interface precisely specified and compile-time
checkable. The C++20 style of requirements specification:

```cpp
template<typename Iter, typename Val>
  requires input_iterator<Iter> && equality_comparable_with<iter_value_t<Iter>, Val>
Iter find(Iter first, Iter last, Val v)
{
    // ...
}
```

Without the constraint, the requirements on `Iter` and `Val` exist only as
whatever the body happens to use, and are reported only as errors deep inside
the instantiation.

## Decision

Constrain the template parameters of any template that forms an interface, using
concepts.

- Prefer a standard-library concept where one names the requirement.
- Where none does, define a named concept rather than leaving the requirement
  implicit in the body or expressed as a raw `requires` expression repeated at
  each use.
- Non-variadic template parameters of interface templates are constrained;
  unconstrained `typename` is for implementation details and for genuinely
  unconstrained parameters, and is worth a second look either way.

## Rationale

A template's parameter list is its interface, and an unconstrained one documents
nothing: the actual contract is "whatever compiles", discovered by a caller from
an error message pointing at a line inside the template. A concept moves the
diagnosis to the call, in terms of a named requirement that was not met.

This is [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) for
generic code — the same argument, with concepts playing the part types play for
non-template parameters.

## Consequences

### Positive

- Errors are reported at the call site, against a named requirement, instead of
  inside the instantiation.
- The requirements on a type are readable from the declaration.
- Overloads can be distinguished by constraint rather than by SFINAE tricks.

### Negative

- Naming the right concept is design work, and a concept that is too tight
  rejects valid uses while one that is too loose documents nothing.
- Constraints are part of the interface, so tightening one later is a breaking
  change.
- Retrofitting concepts to existing templates is a substantial edit for no
  behavioural gain.

### Neutral

- This record does not require a concept for every template — only for those
  that form an interface.
- C++20 is the assumed baseline, since that is the revision in which concepts
  became a language feature rather than a technical specification.

## Enforcement

- Warn if any non-variadic template parameter is not constrained by a concept,
  in its declaration or mentioned in a `requires` clause.

## Related

- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the
  non-generic version of the same rule.
- [0024](0024-i-5-state-preconditions-if-any.md) — a concept is a precondition
  that the compiler checks.
