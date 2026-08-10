# 0441. F.4: If a function might have to be evaluated at compile time, declare it constexpr

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

`constexpr` is what tells the compiler to allow compile-time evaluation. The
infamous factorial:

```cpp
constexpr int fac(int n)
{
    constexpr int max_exp = 17;      // constexpr enables max_exp to be used in Expects
    Expects(0 <= n && n < max_exp);  // prevent silliness and overflow
    int x = 1;
    for (int i = 2; i <= n; ++i) x *= i;
    return x;
}
```

`constexpr` does not guarantee compile-time evaluation; it guarantees only that
the function *can* be evaluated at compile time for constant-expression
arguments, if the programmer requires it or the compiler decides to do so as an
optimisation:

```cpp
constexpr int min(int x, int y) { return x < y ? x : y; }

void test(int v)
{
    int m1 = min(-1, 2);            // probably compile-time evaluation
    constexpr int m2 = min(-1, 2);  // compile-time evaluation
    int m3 = min(-1, v);            // run-time evaluation
    constexpr int m4 = min(-1, v);  // error: cannot evaluate at compile time
}
```

Do not try to make all functions `constexpr`. Most computation is best done at
run time. In particular, any API that might eventually depend on high-level
run-time configuration or on business logic should not be made `constexpr`: such
customisation cannot be evaluated by the compiler, and any `constexpr` function
that depended on that API would later have to be refactored or drop `constexpr`
again.

## Decision

Declare a function `constexpr` when a caller might legitimately need its result
as a constant expression.

- Mark small, self-contained computations over values — arithmetic, conversions,
  lookups over literal data — as `constexpr`.
- Do not mark a function `constexpr` merely because the compiler accepts it;
  require an actual or plausible constant-expression caller.
- Do not mark anything `constexpr` that sits on a path which may later consult
  configuration, the file system, the clock, or user settings.
- Where the result must be computed at compile time, declare the receiving
  variable `constexpr` too; the keyword on the function alone does not force it.
- Remember that `constexpr` is part of the interface: removing it later breaks
  callers that relied on constant evaluation.

## Rationale

The keyword is a promise to callers, not an optimisation request — this is the
one thing about it that is routinely misread. `constexpr int m2 = min(-1, 2);`
compiles only because `min` made that promise, so adding the keyword widens the
contract and removing it is a breaking change. That asymmetry is the reason the
rule is stated as "might have to be" rather than "can be": the cost of the
keyword is paid later, by whoever must keep the promise, and a function whose
implementation will one day want to read a setting cannot keep it.

## Consequences

### Positive

- Constants that were previously computed at run time, or hand-computed and
  written as literals, can be computed by the compiler from readable code.
- A `constexpr` function is usable in contexts that require constant
  expressions: array bounds, template arguments, `Expects` on other `constexpr`
  functions.
- Constant evaluation diagnoses undefined behaviour it encounters, so some bugs
  become compile errors.
- `constexpr` implies `inline`, so the definition can live in a header without
  linkage problems.

### Negative

- The keyword is a compatibility commitment that is easy to add thoughtlessly
  and painful to withdraw, because withdrawal breaks callers rather than the
  function itself.
- It pushes definitions into headers, which increases coupling and compile times
  and exposes the implementation of what might have been a private computation.
- Constant evaluation has its own restricted rules; keeping a function within
  them can distort an implementation that would otherwise be straightforward.
- Marking a function `constexpr` guarantees nothing about run-time performance,
  yet it reads to many people as if it did.

### Neutral

- The rule is silent on how much of a codebase should be `constexpr`; the note
  that most computation belongs at run time is the only guidance given.
- `constexpr` and `noexcept` interact: a `constexpr` function can still throw
  when evaluated at run time.

## Enforcement

Impossible and unnecessary. The compiler gives an error if a non-`constexpr`
function is called where a constant is required.

## Related

- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — the
  general preference this rule serves.
- [0442](0442-f-5-if-a-function-is-very-small-and-time-critical-declare-it-inline.md)
  — `constexpr` implies `inline`, with the ABI consequence that carries.
- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — a computed
  constant is the most immutable form of data available.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — work moved to compile time is
  work not repeated at run time.
