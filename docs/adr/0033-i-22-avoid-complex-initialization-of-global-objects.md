# 0033. I.22: Avoid complex initialization of global objects

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Complex initialisation can lead to undefined order of execution:

```cpp
// file1.c
extern const X x;
const Y y = f(x);   // read x; write y

// file2.c
extern const Y y;
const X x = g(y);   // read y; write x
```

Since `x` and `y` are in different translation units, the order of the calls to
`f()` and `g()` is undefined; one of them will access an uninitialised `const`.
This also shows that the order-of-initialisation problem for namespace-scope
objects is not limited to variables that are mutable.

Order-of-initialisation problems become particularly difficult to handle in
concurrent code. It is usually best to avoid namespace-scope objects altogether.

## Decision

Avoid complex initialisation of objects at namespace scope.

- Initialise a namespace-scope object with a constant expression; prefer
  `constexpr`, and prefer a literal type that needs no dynamic initialisation at
  all.
- Do not call a non-`constexpr` function to initialise one.
- Do not read another translation unit's object to initialise one.
- Where an object genuinely needs non-trivial construction, give it
  initialisation on first use through a function-local `static`, or make it
  owned by something with an explicit lifetime.

## Rationale

The failure here is not a bug in the code as written; each file is individually
reasonable, and the program is broken by the linker's choice of order. That
makes it undetectable by reading either file, and it can change with a build
setting, a link order, or an unrelated new file.

The `const` detail is what makes this rule worth its own record rather than
being folded into
[0021](0021-i-2-avoid-non-const-global-variables.md): making the object
immutable, which fixes the mutation problem, does nothing at all for the
initialisation-order problem.

## Consequences

### Positive

- A class of failure that depends on link order, and cannot be found by reading
  a translation unit, is removed.
- Constant initialisation happens before any code runs, so nothing can observe a
  half-built object.
- Startup gets cheaper and more predictable.

### Negative

- Initialisation on first use adds a check on every access, and moves the cost
  to an arbitrary point at runtime rather than to startup.
- Some objects genuinely need construction — a registry, a cached configuration
  — and pushing them into functions or into explicitly owned objects is more
  code than the namespace-scope definition was.
- Destruction order remains undefined at the other end of the program's life,
  and this record does not address it.

### Neutral

- The best fix is usually to not have the namespace-scope object at all, which
  is [0021](0021-i-2-avoid-non-const-global-variables.md)'s business.
- Function-local `static` is permitted here and in
  [0022](0022-i-3-avoid-singletons.md), for exactly this reason.

## Enforcement

- Flag initialisers of globals that call non-`constexpr` functions.
- Flag initialisers of globals that access `extern` objects.

## Related

- [0021](0021-i-2-avoid-non-const-global-variables.md) — the broader rule about
  namespace-scope state.
- [0022](0022-i-3-avoid-singletons.md) — initialisation on first use as the
  sanctioned escape.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) —
  constant initialisation is the compile-time answer.
