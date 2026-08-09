# 0043. F.4: If a function might have to be evaluated at compile time, declare it constexpr

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

`constexpr` appears 71 times across Core and Graphite, almost all of it on
constants — timeouts, glyph ranges, magic strings — rather than on functions.

## Decision

Adopt F.4 as written. Transcribed in full:

> **Reason.** `constexpr` is needed to tell the compiler to allow compile-time
> evaluation.

**Example.** The (in)famous factorial:

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

This is C++14. For C++11, use a recursive formulation of `fac()`.

**Note.** `constexpr` does not guarantee compile-time evaluation; it just
guarantees that the function can be evaluated at compile time for constant
expression arguments if the programmer requires it or the compiler decides to do
so to optimize.

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

**Note.** Don't try to make all functions `constexpr`. Most computation is best
done at run time.

**Note.** Any API that might eventually depend on high-level run-time
configuration or business logic should not be made `constexpr`. Such
customization can not be evaluated by the compiler, and any `constexpr`
functions that depended upon that API would have to be refactored or drop
`constexpr`.

**Enforcement.** Impossible and unnecessary. The compiler gives an error if a
non-`constexpr` function is called where a constant is required.

## Rationale

Adopted as written, with its own third Note taken as seriously as the rule
itself: do not try to make all functions `constexpr`.

That Note is what makes this rule cheap here. Almost everything this program
computes depends on the filesystem, the window, or a file the user edited —
none of it is constant-expression material. The `constexpr` already in the
codebase is on data, which is the right place for it, and no function is a
plausible candidate today.

The fourth Note is the one worth remembering as a hazard: marking a function
`constexpr` is a commitment to a caller that can be hard to withdraw, because
removing it breaks any use in a constant expression.

## Consequences

### Positive

- Costs nothing to adopt: the rule's own guidance is not to over-apply it, and
  the compiler enforces the only case that matters.
- Records that `constexpr`-on-constants is the established use here, so the
  count does not look like an oversight.

### Negative

- The rule's hazard is real and asymmetric: adding `constexpr` is easy, removing
  it is a breaking change for callers. Being liberal with it now would create
  obligations later.

### Neutral

- Enforcement is "impossible and unnecessary" by the rule's own admission;
  nothing will check this.
- No deviation is recorded, because there is no function in the repository whose
  arguments could be constant expressions in practice.

## Related

- [0042](0042-f-3-keep-functions-short-and-simple.md) — the previous rule.
- [0044](0044-f-5-if-a-function-is-very-small-and-time-critical-declare-it-inline.md)
  — the next rule; `constexpr` implies `inline`.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — P.5,
  the philosophy this serves.
