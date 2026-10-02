# 0191. ES.28: Use lambdas for complex initialization, especially of const variables

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

A lambda used as an initialiser encapsulates local initialisation neatly,
including the cleaning up of scratch variables needed only for the
initialisation, without needing to create a needless non-local yet non-reusable
function. It also works for variables that should be `const`, but only after
some initialisation work has been done.

```cpp
widget x;   // should be const, but:
for (auto i = 2; i <= N; ++i) {          // this could be some
    x += some_obj.do_something_with(i);  // arbitrarily long code
}                                        // needed to initialize x
// from here, x should be const, but we can't say so in code in this style
```

The comment on the last line is the whole problem. The author knows `x` is
finished; the compiler does not, and no reader can tell without checking every
subsequent line. Wrapping the computation makes the claim expressible:

```cpp
const widget x = [&] {
    widget val;                                // assume that widget has a default constructor
    for (auto i = 2; i <= N; ++i) {            // this could be some
        val += some_obj.do_something_with(i);  // arbitrarily long code
    }                                          // needed to initialize x
    return val;
}();
```

`val` exists only inside the lambda and cannot be reached afterwards, and `x` is
`const` from its first line. There is a caveat attached: if at all possible,
reduce the conditions to a simple set of alternatives — an enumeration, for
instance — and do not mix up selection and initialisation.

## Decision

Where a value takes more than one expression to compute, compute it in an
immediately invoked lambda and bind the result.

- Write `const T x = [&] { ...; return val; }();` rather than declaring `x`
  mutable and filling it in afterwards.
- Keep the scratch variables of the computation inside the lambda so they do not
  survive it.
- Use this in preference to a helper function when the computation is used once
  and would otherwise become a non-local name with no other caller.
- Use it in preference to a chain of assignments across branches, which is the
  shape
  [0185](0185-es-22-dont-declare-a-variable-until-you-have-a-value-to-initialize-it-with.md)
  warns about.
- Capture by reference (`[&]`) for an immediately invoked lambda; it runs before
  anything it captured can go out of scope. Do not let this habit carry over to
  a lambda that is stored or deferred.
- Prefer reducing the selection to an enumeration and initialising from that,
  where the complexity is a set of alternatives rather than a computation.

## Rationale

This rule exists because two other rules — initialise at declaration, and make
it `const` — are otherwise unsatisfiable together whenever the value takes work
to produce. The lambda is the mechanism that makes them compatible, at the cost
of two characters of syntax. Its secondary effect is often the more valuable
one: the scratch variables of the computation stop leaking into the enclosing
scope, so the function's remaining locals are only the ones that matter.

## Consequences

### Positive

- `const` becomes available for values that are computed rather than given.
- Temporaries of the computation are confined to it, keeping the enclosing scope
  small.
- No non-local helper function is created for a computation with exactly one
  caller.

### Negative

- The `[&] { ... }()` idiom is not universally familiar, and the trailing `()`
  is easy to overlook; a reader who misses it sees a lambda being assigned
  rather than a value.
- It nests a function body inside a declaration, so the declaration can become
  long and the surrounding statement structure harder to see.
- The lambda must return a single value, so a computation producing two related
  results needs a `pair`, a `tuple`, or a struct, plus a structured binding at
  the other end.
- It is easy to overuse: a two-line computation wrapped in a lambda is more
  ceremony than the assignment it replaced, and the rule offers no threshold.
- Debugging steps into an unnamed function, and a stack frame for a lambda is
  less informative than one for a named helper.

### Neutral

- A named helper function remains the better answer when the computation has, or
  will have, more than one caller.
- The upstream advice to reduce alternatives to an enumeration is about not
  needing this rule in the first place.

## Enforcement

Hard; at best a heuristic. Look for an uninitialised variable followed by a loop
that assigns to it.

## Related

- [0183](0183-es-20-always-initialize-an-object.md) — the rule this mechanism
  makes satisfiable for computed values.
- [0185](0185-es-22-dont-declare-a-variable-until-you-have-a-value-to-initialize-it-with.md)
  — the branch-and-assign shape this replaces.
- [0188](0188-es-25-declare-an-object-const-or-constexpr-unless-you-want-to-modify-its-value-later-on.md)
  — the `const` this rule exists to preserve.
- [0175](0175-es-5-keep-scopes-small.md) — confining the scratch variables is the
  same goal.
