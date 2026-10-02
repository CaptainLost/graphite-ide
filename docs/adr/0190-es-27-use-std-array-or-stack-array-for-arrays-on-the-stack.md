# 0190. ES.27: Use std::array or stack_array for arrays on the stack

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

These types are readable, they do not implicitly convert to pointers, and they
are not confused with non-standard extensions of built-in arrays.

```cpp
const int n = 7;
int m = 9;

void f()
{
    int a1[n];
    int a2[m];   // error: not ISO C++
    // ...
}
```

The definition of `a1` is legal C++ and always has been; there is a great deal
of such code. It is error-prone all the same, especially when the bound is
non-local, and it is a popular source of errors: buffer overflow, and pointers
obtained from array decay. The definition of `a2` is a variable-length array —
valid C, not valid C++, accepted by some compilers as an extension, and
considered a security risk.

The replacements:

```cpp
const int n = 7;
int m = 9;

void f()
{
    array<int, n> a1;
    stack_array<int> a2(m);
    // ...
}
```

`std::array<int, n>` carries its size in its type, knows its own `size()`, and
does not decay to `int*` when passed to a function. `stack_array` is the
Guidelines Support Library's answer for a run-time bound, and requires that
library; where it is not available, a `std::vector` with a reserved capacity is
the ordinary substitute, at the cost of a heap allocation.

## Decision

Do not declare a built-in array as a local variable.

- Use `std::array<T, N>` where the bound is a compile-time constant.
- Do not use a variable-length array. Where the bound is only known at run time,
  use `stack_array<T>` if the Guidelines Support Library is available, and
  otherwise `std::vector<T>`.
- Prefer a local constant bound over one declared elsewhere; a bound that is
  non-local is a bound the reader cannot check against the uses.
- Pass such an array on as a `std::span` or a reference, never by letting it
  decay, per
  [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md).
- Where a C API requires a raw pointer and a length, take them from the array —
  `a.data()` and `a.size()` — at the call, rather than declaring a raw array to
  begin with.

## Rationale

The decisive property is that `std::array` does not decay. A built-in array
loses its size at the first function call it is passed to, and everything after
that point is a pointer with a length carried separately by convention — which
is the situation
[0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) exists to prevent.
Choosing the type at the declaration is what makes that prevention possible;
once a raw array exists, its decay is one call away. The variable-length array
is a separate matter and simpler: it is not ISO C++ at all, which
[0008](0008-p-2-write-in-iso-standard-c.md) already forbids.

## Consequences

### Positive

- The size travels with the array, so `size()` is available and correct wherever
  the object is.
- Array-to-pointer decay stops happening silently, which removes the main way an
  extent gets lost.
- Non-standard variable-length arrays disappear along with the portability and
  stack-exhaustion risks they carry.

### Negative

- `std::array` requires a compile-time bound, and the run-time case has no
  standard stack-allocated answer: `stack_array` means depending on the GSL, and
  `std::vector` means a heap allocation the raw array did not have.
- Aggregate initialisation of `std::array` is fussier than for a built-in array,
  particularly for arrays of arrays, which need an extra pair of braces.
- Passing to a C API still needs `.data()`, so the raw pointer reappears exactly
  where the checking was wanted.
- Flagging non-local constant bounds, as the enforcement suggests, will fire on
  perfectly reasonable code that shares a size constant between a declaration
  and its consumers.

### Neutral

- The rule is about arrays on the stack. Dynamically sized storage is a
  different question, answered by the containers.
- `std::array` has the same layout and cost as a built-in array; nothing is paid
  for the type.

## Enforcement

- Flag arrays with non-constant bounds (C-style variable-length arrays).
- Flag arrays with non-local constant bounds.

## Related

- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the interface
  rule that this declaration rule makes possible.
- [0008](0008-p-2-write-in-iso-standard-c.md) — variable-length arrays are not
  ISO C++.
- [0186](0186-es-23-prefer-the-initializer-syntax.md) — how a `std::array` is
  initialised.
- [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md)
  — preferring the standard type to the built-in construct.
