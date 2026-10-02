# 0131. C.166: Overload unary & only as part of a system of smart pointers and references

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The `&` operator is fundamental in C++, and many parts of the language's
semantics assume its default meaning.

```cpp
class Ptr { // a somewhat smart pointer
    Ptr(X* pp) : p(pp) { /* check */ }
    X* operator->() { /* check */ return p; }
    X operator[](int i);
    X operator*();
private:
    T* p;
};

class X {
    Ptr operator&() { return Ptr{this}; }
    // ...
};
```

If you "mess with" `operator&`, be sure that its definition has matching
meanings for `->`, `[]`, `*` and `.` on the result type. Note that `operator.`
currently cannot be overloaded, so a perfect system is impossible; the committee
proposal known as Operator Dot exists to remedy that but is not in the language.
Note also that `std::addressof()` always yields a built-in pointer.

The point is that taking the address of an object is not an isolated operation.
Generic code, the standard library and ordinary utility functions all assume
`&x` produces something that behaves as a pointer to `x`. Returning a proxy
instead is coherent only if that proxy supports the whole family of operations
that a pointer supports, so that everything downstream continues to work.

## Decision

Do not overload unary `operator&` except as part of a deliberate,
complete pointer-and-reference system.

- Treat `&` as reserved for its built-in meaning in ordinary types.
- Where a proxy pointer system genuinely requires it, define `operator*`,
  `operator->` and, where indexing is meaningful, `operator[]` on the result
  type with consistent semantics, and document the whole system as one design.
- Accept that the system cannot be complete, because `operator.` cannot be
  overloaded, and say where the gap is.
- In any code that must obtain a real address of an object of an unknown type,
  use `std::addressof(x)` rather than `&x`, since `addressof` always yields a
  built-in pointer.

## Rationale

`&` differs from the other overloadable operators in that its result is consumed
by code that was never written with the type in mind. An unconventional `+` on a
type misleads whoever reads the expression; an unconventional `&` changes what
every generic algorithm, container implementation and utility function gets when
it asks where the object lives. That is why the rule is not "define it
conventionally" but "define it only as part of a system": there is no way to
make one operator's departure local.

## Consequences

### Positive

- `&x` continues to mean the address of `x` throughout ordinary code.
- Where a proxy system is genuinely wanted, the rule forces it to be designed as
  a system rather than assembled one operator at a time.
- `std::addressof` gives generic code a way to be correct regardless of what a
  type did.

### Negative

- The permitted design cannot be completed, since `operator.` is not
  overloadable, so any such system has a hole that its users will eventually
  fall into.
- Requiring `std::addressof` in generic code is a permanent tax paid by every
  template, imposed by a rule almost nobody breaks.
- Enforcement is described upstream as tricky, and the suggested check —
  requiring `->` on the result type — verifies the shape of the system and not
  its coherence.

### Neutral

- The rule concerns unary `&` only; the binary bitwise `&` is a separate
  operator with its own conventions.
- Modern designs tend to use a named accessor rather than a proxy `&` at all,
  which sidesteps the rule entirely.

## Enforcement

- Tricky. Warn if `&` is user-defined without `->` also being defined for the
  result type.

## Related

- [0125](0125-c-160-define-operators-primarily-to-mimic-conventional-usage.md) —
  the general rule; `&` is its most consequential instance.
- [0132](0132-c-167-use-an-operator-for-an-operation-with-its-conventional-meaning.md)
  — access operations are named there as having strong conventions.
- [0129](0129-c-164-avoid-implicit-conversion-operators.md) — the other operator
  whose misuse changes what surrounding code silently receives.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — a proxy pointer system is a messy construct, and must be contained as one.
