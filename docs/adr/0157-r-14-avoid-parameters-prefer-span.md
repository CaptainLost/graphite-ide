# 0157. R.14: Avoid [] parameters, prefer span

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

An array decays to a pointer, thereby losing its size and opening the
opportunity for range errors. A `span` preserves the size information.

```cpp
void f(int[]);          // not recommended
void f(int*);           // not recommended for multiple objects
                        // (a pointer should point to a single object, do not
                        // subscript)
void f(gsl::span<int>); // good, recommended
```

The first declaration is the one worth dwelling on, because it looks as though
it says something. `int[]` in a parameter list is not an array type at all: the
language rewrites it to `int*`, so the brackets are a comment that the compiler
strips. Even a bound is stripped — `void f(int[10])` and `void f(int*)` declare
the same function, and calling it with an array of three elements is
well-formed.

Since C++20 the vocabulary type for this is in the standard library as
`std::span`, so the `gsl::` qualification in the upstream example is no longer
needed.

## Decision

Do not declare a parameter with array syntax.

- Take a `std::span<T>` for a mutable sequence, `std::span<const T>` for one
  that is only read, and `std::string_view` for a character sequence.
- Where the length is fixed and known, `std::span<T, N>` or a reference to
  `std::array<T, N>` states it in a way the compiler checks.
- Reserve `T*` for a single object.
- Keep an array-syntax or `(pointer, count)` parameter only where an external
  ABI requires it, and convert to a span immediately inside the boundary.

## Rationale

This rule costs nothing and removes a construct that actively misleads. The
brackets in `void f(int[10])` read as a promise about the argument and are
discarded by the language before any checking happens, so the declaration
misinforms every reader who has not memorised the decay rule. A `span` carries
the extent as data, which is the property the brackets were pretending to have.

## Consequences

### Positive

- The extent travels with the data, so the callee can range-check and the caller
  cannot get the count wrong.
- A declaration stops implying a bound the language does not enforce.
- Call sites shorten, because the extent is deduced from the argument.

### Negative

- `span` is a non-owning view with no lifetime guarantees, so a dangling `span`
  is as easy to make as a dangling pointer, and reads as safer.
- Bounds are checkable rather than checked; whether an implementation checks
  them depends on the standard library and the build configuration.
- It does not cross a C ABI, so the old form survives at the boundaries.
- A `span` is two words rather than one, which is a real cost in a hot call, and
  `span<T, N>` recovers the size at the price of the type varying with the
  length.

### Neutral

- This rule concerns parameters. A member or a local may still be a real array
  type, where the bound is not discarded.
- Whether the sequence is owned by the caller is a separate question; `span`
  never owns.

## Enforcement

- Flag `[]` parameters. Suggest `span` instead.

## Related

- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the interface
  rule with the fuller argument and the slicing example.
- [0148](0148-r-2-in-interfaces-use-raw-pointers-to-denote-individual-objects-only.md)
  — why `T*` is left meaning one object.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — a
  bound the compiler can use beats one written in a comment.
