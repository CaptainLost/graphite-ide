# 0361. T.123: Use constexpr functions to compute values at compile time

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

A function is the most obvious and conventional way of expressing the
computation of a value, and a `constexpr` function often implies less
compile-time overhead than the alternatives — a recursive class template with a
static `value` member, or a chain of specialisations.

```cpp
template<typename T>
    // requires Number<T>
constexpr T pow(T v, int n)   // power/exponential
{
    T res = 1;
    while (n--) res *= v;
    return res;
}

constexpr auto f7 = pow(pi, 7);
```

The same computation as a metaprogram would be a template with a base-case
specialisation, would be written in a different language from the rest of the
function's body, and could not be called at run time at all.

The traits techniques have mostly been replaced by template aliases to compute
types and `constexpr` functions to compute values.

## Decision

Compute compile-time values with ordinary functions marked `constexpr`.

- Write the computation as a normal function body — loops, locals, branches —
  and mark it `constexpr`.
- Do not write a recursive class template whose result is a static data member
  when a function will do.
- Initialise the result into a `constexpr` variable where the value is required
  at compile time, so the compiler checks that it really was computable.
- Use `consteval` where a run-time call would be a mistake, and `constinit`
  where the point is initialisation order rather than constancy.

## Rationale

A `constexpr` function is one definition that serves both worlds: the same
source computes the value during translation when its arguments allow it, and
at run time when they do not. A metaprogram computing the same value is a
second implementation of it, written in a dialect with no loops and no mutable
locals, and it cannot be tested by calling it.

## Consequences

### Positive

- The computation is written and read as ordinary code.
- One implementation serves compile-time and run-time callers, so they cannot
  disagree.
- Compile times are typically better than for the equivalent metaprogram.

### Negative

- `constexpr` on a function is permission, not a guarantee: unless the result is
  bound to a `constexpr` variable or used where a constant is required, the
  call may simply happen at run time, and nothing says so.
- It is a public commitment. Marking a function `constexpr` constrains its body
  for every future maintainer, and withdrawing the keyword breaks callers who
  relied on it.
- Compile-time evaluation cannot be stepped through in a debugger, and a
  constant expression that fails is reported as a wall of notes rather than as a
  failing computation.
- Heavy compile-time evaluation moves cost into every build, which is a real
  price paid by everyone including people who never call the function.

### Neutral

- The rule is about values; type computation belongs to the alias rule, and the
  two are stated together upstream.
- Whether a given call is evaluated during translation is an
  implementation-quality question outside a constant-expression context.

## Enforcement

- Flag template metaprograms yielding a value; these should be replaced with
  `constexpr` functions.

## Related

- [0315](0315-con-5-use-constexpr-for-values-that-can-be-computed-at-compile-time.md)
  — the same preference stated for objects rather than functions.
- [0441](0441-f-4-if-a-function-might-have-to-be-evaluated-at-compile-time-declare-it-constexpr.md)
  — when to put the keyword on a function.
- [0246](0246-per-11-move-computation-from-run-time-to-compile-time.md) — the
  performance argument for doing this at all.
- [0360](0360-t-122-use-templates-usually-template-aliases-to-compute-types-at-compile-time.md)
  — the type half of the same guidance.
