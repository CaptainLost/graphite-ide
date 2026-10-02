# 0324. T.13: Prefer the shorthand notation for simple, single-type argument concepts

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Readability, and direct expression of an idea. To say "`T` is sortable":

```cpp
template<typename T>       // Correct but verbose: "The parameter is
    requires sortable<T>   // of type T which is the name of a type
void sort(T&);             // that is sortable"

template<sortable T>       // Better: "The parameter is of type T
void sort(T&);             // which is Sortable"

void sort(sortable auto&); // Best: "The parameter is Sortable"
```

The shorter versions better match the way the requirement is spoken aloud. Note
that many templates do not need to use the `template` keyword at all.

## Decision

Write a single-type constraint in the shortest form that still names the
parameter you need.

- Prefer the abbreviated function template form, `void f(sortable auto& s)`, when
  the parameter type is not referred to again.
- Use a constrained template parameter, `template<sortable T>`, when the type
  needs a name — for a return type, a nested alias, or a second parameter.
- Reserve a `requires` clause for constraints the shorthand cannot express: those
  relating two parameters, those combining several concepts with awkward
  precedence, or those needing a `requires`-expression inline.
- Do not introduce a `typename T` and then constrain it with a simple
  single-type-argument concept in a separate clause; that is the verbose form the
  rule exists to displace.

## Rationale

All three spellings mean the same thing, so the only axis left is how quickly a
reader recovers the intent, and the shorthand puts the concept where the reader
looks for a type. The rule also has a practical edge the examples show
incidentally: the abbreviated form removes the `template` header entirely, so a
constrained generic function stops looking like a metaprogramming construct and
starts looking like an ordinary function.

## Consequences

### Positive

- The declaration reads as the sentence a programmer would say.
- Constraints stay visible next to the parameter instead of trailing after the
  signature.
- Short declarations make an unconstrained parameter conspicuous, which supports
  [0321](0321-t-10-specify-concepts-for-all-template-arguments.md).

### Negative

- The abbreviated form gives the parameter type no name, so adding a second
  parameter of the same type, or naming the type in the body, means rewriting
  the declaration in a longer form.
- Two `sortable auto` parameters are two independent types, not one, which is a
  genuine trap for readers who read the shorthand as a single `T`.
- Mixing all three notations across a codebase is worse than consistently using
  any one of them, and this rule guarantees a mixture, since each form remains
  correct in its own place.
- Upstream concedes the rule is not enforceable in the short term while code is
  still being converted from `<typename T>`.

### Neutral

- Nothing here changes what is constrained, only how the constraint is spelled.
- For constraints over more than one type, the long form remains the only option
  and no preference is expressed.

## Enforcement

- Not feasible in the short term, while code is being converted from the
  `<typename T>` and `<class T>` notation.
- Later, flag declarations that first introduce a `typename` and then constrain
  it with a simple, single-type-argument concept.

## Related

- [0321](0321-t-10-specify-concepts-for-all-template-arguments.md) — the rule
  that requires the constraint this record formats.
- [0322](0322-t-11-whenever-possible-use-standard-concepts.md) — the concept
  names that go in the shorthand.
- [0323](0323-t-12-prefer-concept-names-over-auto-for-local-variables.md) — the
  same `concept auto` notation applied to local variables.
- [0009](0009-p-3-express-intent.md) — the readability argument this rule rests
  on.
