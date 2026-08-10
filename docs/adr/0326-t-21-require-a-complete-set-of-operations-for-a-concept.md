# 0326. T.21: Require a complete set of operations for a concept

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Ease of comprehension, improved interoperability, and help for implementers and
maintainers. This is a specific variant of the general rule that a concept must
make semantic sense.

```cpp
template<typename T> concept Subtractable = requires(T a, T b) { a-b; };
```

This makes no semantic sense: at least `+` is needed to make `-` meaningful and
useful. Examples of complete sets are `Arithmetic` with `+`, `-`, `*`, `/`, `+=`,
`-=`, `*=` and `/=`, and `Comparable` with `<`, `>`, `<=`, `>=`, `==` and `!=`.

The rule applies whether or not direct language support for concepts is used. It
is a general design rule that even applies to non-templates:

```cpp
class Minimal {
    // ...
};

bool operator==(const Minimal&, const Minimal&);
bool operator<(const Minimal&, const Minimal&);
Minimal operator+(const Minimal&, const Minimal&);
// no other operators

void f(const Minimal& x, const Minimal& y)
{
    if (!(x == y)) { /* ... */ }    // OK
    if (x != y) { /* ... */ }       // surprise! error
    while (!(x < y)) { /* ... */ }  // OK
    while (x >= y) { /* ... */ }    // surprise! error
    x = x + y;          // OK
    x += y;             // surprise! error
}
```

This is minimal, but surprising and constraining for users, and it could even be
less efficient. The rule supports the view that a concept should reflect a
mathematically coherent set of operations.

```cpp
class Convenient {
    // ...
};

bool operator==(const Convenient&, const Convenient&);
bool operator<(const Convenient&, const Convenient&);
// ... and the other comparison operators ...
Minimal operator+(const Convenient&, const Convenient&);
// ... and the other arithmetic operators ...

void f(const Convenient& x, const Convenient& y)
{
    if (!(x == y)) { /* ... */ }    // OK
    if (x != y) { /* ... */ }       // OK
    while (!(x < y)) { /* ... */ }  // OK
    while (x >= y) { /* ... */ }    // OK
    x = x + y;     // OK
    x += y;        // OK
}
```

It can be a nuisance to define all the operators, but it is not hard. Ideally the
language would supply the comparison operators by default.

## Decision

Require, and provide, whole operation sets rather than fragments.

- When defining a concept, include the full coherent set: all six comparisons for
  an ordering, the arithmetic operators together with their compound-assignment
  forms for a numeric type.
- Do not define a concept whose entire content is one operation that presupposes
  another.
- Apply the same standard to concrete classes: a type that has `==` but not `!=`,
  or `+` but not `+=`, surprises its users.
- In C++20, use `operator<=>` and a defaulted `operator==` to obtain the complete
  comparison set without writing each operator out.

## Rationale

An incomplete operation set does not restrict users to the subset provided — it
makes the missing members compile errors at the point of use, so every caller
discovers the gap individually and works around it locally. The workaround is
also frequently worse than the missing operator would have been: `x = x + y`
where `x += y` was meant is a copy the author did not ask for, which is why
upstream notes the minimal class "could even be less efficient".

## Consequences

### Positive

- Users can write the expression they mean instead of the one the type happens to
  support.
- A concept that names a whole set is far less likely to match an unrelated type
  by accident.
- Generic algorithms can be written against the natural operation without adding
  a constraint for each operator they touch.
- Hand-written workarounds for missing operators disappear, along with the copies
  some of them cost.

### Negative

- It is boilerplate: for a type that genuinely only needs equality, the other
  operators are dead code that still has to be written, reviewed and tested.
- Some abstractions are honestly partial — an ordering with no meaningful
  `operator+`, a duration type where multiplication of two durations is
  nonsense — and forcing a complete set would mean defining operations with no
  meaning, which
  [0132](0132-c-167-use-an-operator-for-an-operation-with-its-conventional-meaning.md)
  forbids.
- Requiring the whole set makes the concept stricter than most algorithms need,
  in tension with
  [0333](0333-t-41-require-only-essential-properties-in-a-templates-concepts.md).
- Upstream admits `std::string` is "odd" and will not be fixed, so the standard
  library itself generates false positives for any check.

### Neutral

- What counts as complete is defined by the abstraction, not by a fixed list; the
  arithmetic and comparison sets are examples rather than the whole catalogue.
- The rule constrains both concept definitions and ordinary class design.

## Enforcement

- Flag classes that support odd subsets of a set of operators — `==` but not
  `!=`, or `+` but not `-`. `std::string` is such a case, but it is too late to
  change that.

## Related

- [0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md) — the general
  rule this one specialises.
- [0049](0049-c-11-make-concrete-types-regular.md) — the same completeness
  argument for concrete types.
- [0127](0127-c-162-overload-operations-that-are-roughly-equivalent.md) and
  [0132](0132-c-167-use-an-operator-for-an-operation-with-its-conventional-meaning.md)
  — the limit: completeness never justifies an operator with no natural meaning.
- [0333](0333-t-41-require-only-essential-properties-in-a-templates-concepts.md)
  — the pull in the opposite direction.
