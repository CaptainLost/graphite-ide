# 0365. T.141: Use an unnamed lambda if you need a simple function object in one place only

Status: superseded by 0561
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

An unnamed lambda makes the code concise and gives better locality than the
alternatives: the operation is written where it is used, so the reader does not
have to go anywhere to find out what the algorithm is doing.

```cpp
auto earlyUsersEnd = std::remove_if(users.begin(), users.end(),
                                    [](const User &a) { return a.id > 100; });
```

Naming the predicate here would add a declaration, a name to choose and a place
to put it, in exchange for nothing: the operation is two tokens of logic and
has no meaning outside this call.

The exception is that naming a lambda can be useful for clarity even when it is
used only once — when the name explains a condition that the expression does
not.

## Decision

Write the function object inline, without a name, when it is simple and used in
one place.

- Pass a lambda directly as the algorithm's argument rather than declaring it
  on the line above.
- Capture only what the body needs, and prefer explicit captures to a default.
- Give it a name when the name would explain something the expression does not,
  or when the body has grown enough that the call site is no longer readable.
- Give it a name as soon as a second call site needs the same operation.

## Rationale

Locality is the whole argument: a predicate written in the call reads as part
of the call, and the reader confirms in one glance that the algorithm and the
condition match. Every name introduced is a small tax — chosen, scoped,
maintained and kept accurate — and for a one-line predicate used once, the tax
buys nothing.

## Consequences

### Positive

- The condition is visible exactly where it applies.
- No name is invented for something that does not need one, so nothing has to
  be kept accurate.
- The operation cannot be called from somewhere it was not designed for.

### Negative

- An unnamed lambda cannot be unit tested, reused or referred to in a bug
  report; the only way to exercise it is through its call site.
- "Simple" and "one place only" both decay. Bodies grow and second call sites
  appear, and nothing prompts the extraction at the moment it becomes due, so
  near-duplicate lambdas accumulate.
- Inline bodies obscure the shape of the enclosing call once they exceed a line
  or two — the algorithm, its range and its predicate stop being visible
  together.
- Diagnostics mentioning a closure type are unhelpful, since the type has no
  name to report.

### Neutral

- This rule and the rule about naming reusable operations are two ends of the
  same judgement; neither can state the threshold.
- Naming for clarity is explicitly allowed, so the rule is a default rather
  than a prohibition.

## Enforcement

- Look for identical and near-identical lambdas, to be replaced with named
  functions or named lambdas.

## Related

- [0364](0364-t-140-name-all-operations-with-potential-for-reuse.md) — the
  opposing pull, and the one that wins as soon as reuse appears.
- [0468](0468-f-50-use-a-lambda-when-a-function-wont-do-to-capture-local-variables-or-to-write-a-local-function.md)
  — when a lambda is the right tool in the first place.
- [0470](0470-f-52-prefer-capturing-by-reference-in-lambdas-that-will-be-used-locally-including-passed-to-algorithms.md)
  — how a local, single-use lambda should capture.
- [0134](0134-c-170-if-you-feel-like-overloading-a-lambda-use-a-generic-lambda.md)
  — what to do when one inline lambda must serve several argument types.
