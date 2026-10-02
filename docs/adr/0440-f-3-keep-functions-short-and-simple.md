# 0440. F.3: Keep functions short and simple

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Large functions are hard to read, more likely to contain complex code, and more
likely to have variables in larger than minimal scopes. Functions with complex
control structures are more likely to be long and more likely to hide logical
errors. Consider:

```cpp
double simple_func(double val, int flag1, int flag2)
    // simple_func: takes a value and calculates the expected ASIC output,
    // given the two mode flags.
{
    double intermediate;
    if (flag1 > 0) {
        intermediate = func1(val);
        if (flag2 % 2)
             intermediate = sqrt(intermediate);
    }
    else if (flag1 == -1) {
        intermediate = func1(-val);
        if (flag2 % 2)
             intermediate = sqrt(-intermediate);
        flag1 = -flag1;
    }
    if (abs(flag2) > 10) {
        intermediate = func2(intermediate);
    }
    switch (flag2 / 10) {
    case 1: if (flag1 == -1) return finalize(intermediate, 1.171);
            break;
    case 2: return finalize(intermediate, 13.1);
    default: break;
    }
    return finalize(intermediate, 0.);
}
```

This is too complex. How would you know whether all possible alternatives have
been correctly handled? It breaks other rules as well. Refactoring gives:

```cpp
double func1_muon(double val, int flag)
{
    // ... one branch of the original, named ...
}

double func1_tau(double val, int flag1, int flag2)
{
    // ... the other branch of the original, named ...
}

double simple_func(double val, int flag1, int flag2)
    // simple_func: takes a value and calculates the expected ASIC output,
    // given the two mode flags.
{
    if (flag1 > 0)
        return func1_muon(val, flag2);
    if (flag1 == -1)
        // handled by func1_tau: flag1 = -flag1;
        return func1_tau(-val, flag1, flag2);
    return 0.;
}
```

"It doesn't fit on a screen" is often a good practical definition of "far too
large". One-to-five-line functions should be considered normal. Break large
functions up into smaller cohesive and named functions; small simple functions
are easily inlined where the cost of a function call is significant.

## Decision

Keep a function short enough to read at once and simple enough to reason about
exhaustively.

- Treat a function that does not fit on one screen as too large. Sixty lines by
  a hundred and forty characters is a workable definition of a screen.
- Treat more than about ten logical paths through a function as too complex,
  counting a simple `switch` as one path.
- Extract each arm of a nest of conditionals into a named function and reduce
  the original to a flat sequence of dispatching returns.
- Regard one-to-five-line functions as the normal case, not as an extravagance.
- Do not keep a function large in order to avoid call overhead; a small function
  is the easy one for the compiler to inline.

## Rationale

The decisive question the long example provokes is not "is this readable" but
"how would you know whether all the alternatives are handled". Length and
branching multiply: a body with ten independent conditions has more states than
a reviewer can hold, so correctness stops being something anyone verifies and
becomes something everyone assumes. Shortening the function does not remove the
states, but it distributes them across bodies that can each be checked
completely, which is the only form of checking that actually happens.

## Consequences

### Positive

- Every path through a small function can actually be enumerated by a reader.
- Variables stay in the smallest scope that needs them, because the scope ends
  sooner.
- Small functions inline well, so the decomposition is usually free at run time.
- Named sub-operations give the reviewer a vocabulary for discussing the parts.

### Negative

- Line counts and cyclomatic thresholds are proxies, and code can be gamed under
  both: a sixty-line function split into three twenty-line functions that only
  ever run in sequence is no simpler, merely more scattered.
- Some functions are irreducibly long — a dispatch table, a parser's token
  switch, a state machine — and splitting them hides the shape that made them
  understandable.
- Extracted helpers acquire parameter lists that reintroduce the coupling the
  extraction was supposed to remove.
- Following logic across many small functions costs jumps the reader has to make
  by hand, which is its own kind of complexity.

### Neutral

- The screen-size figure is a convention, not a measurement; the point is the
  order of magnitude.
- This record is about function bodies, not about file or class size.

## Enforcement

- Flag functions that do not "fit on a screen". Sixty lines by a hundred and
  forty characters is roughly the maximum comfortable for a book page.
- Flag functions that are too complex. Cyclomatic complexity is one usable
  measure; try "more than ten logical paths through", counting a simple `switch`
  as one path.

## Related

- [0439](0439-f-2-a-function-should-perform-a-single-logical-operation.md) — a
  function is usually long because it is doing more than one thing.
- [0438](0438-f-1-package-meaningful-operations-as-carefully-named-functions.md)
  — the extraction this record demands, with the naming requirement attached.
- [0442](0442-f-5-if-a-function-is-very-small-and-time-critical-declare-it-inline.md)
  — why decomposition need not cost call overhead.
