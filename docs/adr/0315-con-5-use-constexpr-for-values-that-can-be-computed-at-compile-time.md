# 0315. Con.5: Use constexpr for values that can be computed at compile time

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Con (Constants and immutability), transcribed 2026-08-10

## Context

Four reasons are given, and they are independent of one another: better
performance, better compile-time checking, guaranteed compile-time evaluation,
and no possibility of a race condition.

```cpp
double x = f(2);            // possible run-time evaluation
const double y = f(2);      // possible run-time evaluation
constexpr double z = f(2);  // error unless f(2) can be evaluated at compile time
```

The three lines differ in what they promise. `x` promises nothing. `y` promises
that the value will not change once computed, but says nothing about when it is
computed, and a `const` initialised by a function call is routinely a run-time
call. `z` requires the value to be produced during translation, and the
declaration fails to compile if it cannot be — which is why the strengthening
from `const` to `constexpr` is a check and not merely a hint.

The keyword on the variable and the keyword on the function are different
statements. `constexpr` on `f` permits compile-time evaluation; `constexpr` on
`z` demands it. What makes a function eligible, and why not every function
should be marked, belongs to
[0441](0441-f-4-if-a-function-might-have-to-be-evaluated-at-compile-time-declare-it-constexpr.md).

## Decision

Use `constexpr` rather than `const` for a value whose initialiser is a constant
expression.

- Declare a constant `constexpr` when it is initialised by a literal, an
  arithmetic expression over other constants, or a call to a `constexpr`
  function with constant arguments.
- Where the result must be produced at compile time, put the keyword on the
  variable; marking the function alone leaves the choice to the compiler.
- Prefer a `constexpr` object to a macro or a magic number for a named constant,
  since it has a type, a scope, and a value the debugger can show.
- Fall back to `const` without ceremony when the initialiser is not a constant
  expression or the type is not a literal type; that is not a failure of this
  rule.
- Use `constexpr` for the sizes, limits and tables that other compile-time
  constructs need — array bounds, `static_assert` conditions, template arguments
  — rather than computing them at start-up.

## Rationale

`constexpr` is the only one of these keywords that is enforced in the direction
the author cares about. `const` records an intention that the value will not
change; `constexpr` records a claim about when it exists and refuses to compile
if the claim is false, which makes it the immutability rule that cannot rot
silently. A value fixed during translation is also the one form of data on which
initialisation order, lazy computation and data races are not questions that can
be asked — the guarantees are structural rather than earned by discipline. This
is the concrete case of the preference stated in
[0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md).

## Consequences

### Positive

- The computation happens once, during the build, instead of at every run or at
  start-up.
- A value that cannot be computed at compile time is reported as an error at the
  declaration rather than degrading quietly into a run-time call.
- Constant evaluation diagnoses undefined behaviour it encounters, so some
  overflow and out-of-bounds bugs become compile errors.
- A `constexpr` object cannot participate in a data race and needs no
  initialisation-order reasoning.

### Negative

- The requirement propagates. Making a value `constexpr` requires every function
  in its initialiser to be `constexpr`, which pulls definitions into headers and
  commits their authors to a promise that is painful to withdraw.
- A `constexpr` object must have a literal type, and must not hold storage that
  outlives the constant evaluation which produced it. C++20 made `std::string`
  and `std::vector` usable during constant evaluation, but an allocating
  container still cannot be a `constexpr` object, so such constants must change
  type — to `std::string_view` or `std::array` — rather than merely gain a
  keyword.
- The value is substituted wherever it is used, so changing one header constant
  rebuilds everything that included it, and across a boundary that is not
  rebuilt together the old value silently persists.
- Constant evaluation costs build time, and when it fails the diagnostic points
  at the evaluation rather than at the mistake.

### Neutral

- The rule is about values, not about how much of a program should be evaluated
  at compile time; the guidelines are explicit elsewhere that most computation
  belongs at run time.
- `constexpr` implies `const` for an object, so nothing in
  [0311](0311-con-1-by-default-make-objects-immutable.md) or
  [0314](0314-con-4-use-const-to-define-objects-with-values-that-do-not-change-after-construction.md)
  is given up by using it.

## Enforcement

Flag `const` definitions whose initialisers are constant expressions. The report
is a suggestion: promotion to `constexpr` only compiles where the type is a
literal type and every function in the initialiser is itself `constexpr`.

## Related

- [0441](0441-f-4-if-a-function-might-have-to-be-evaluated-at-compile-time-declare-it-constexpr.md)
  — the companion rule for functions, referenced by this one upstream.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — the
  philosophy rule this is the constant-shaped case of.
- [0246](0246-per-11-move-computation-from-run-time-to-compile-time.md) — the
  same move argued from performance rather than from immutability.
- [0314](0314-con-4-use-const-to-define-objects-with-values-that-do-not-change-after-construction.md)
  — `const` is the fallback when the initialiser is not a constant expression.
