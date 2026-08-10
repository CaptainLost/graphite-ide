# 0445. F.8: Prefer pure functions

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

A pure function is one whose result depends only on its arguments and which has
no observable effect other than returning that result. Pure functions are easier
to reason about, sometimes easier to optimise and even to parallelise, and
sometimes can be memoised.

```cpp
template<class T>
auto square(T t) { return t * t; }
```

The upstream rule is short and offers no further discussion, no exception, and
no enforcement beyond stating that enforcement is not possible. What it is
really asking for is a default: where a computation could be written as a
function of its inputs, write it that way, and reserve state-touching functions
for the places that genuinely manage state.

## Decision

Prefer functions whose output is determined entirely by their inputs.

- Take what the function needs as parameters rather than reading it from a
  global, a static, or an ambient singleton.
- Return the result rather than writing it into a member, an output parameter,
  or a stream chosen by the callee.
- Keep computation separate from the I/O, logging and mutation that surround it,
  so the computing half stays pure even where the calling half cannot.
- Mark a pure function `const` when it is a member, and consider `constexpr`
  when a caller might need its result as a constant.
- Where purity is impossible — a function whose whole purpose is to mutate
  state, drive the UI, or touch the file system — confine the impurity to that
  function rather than spreading it through the ones it calls.

## Rationale

Purity is what makes a function checkable in isolation: the test is a table of
inputs and expected outputs, with no fixture, no ordering, and no teardown. It
is also what makes a call site readable, because a pure call cannot have changed
anything the reader was tracking, so it can be understood without reading the
body. Neither property survives one hidden read of mutable global state, which
is why the rule is worth stating as a default rather than as an optimisation.

## Consequences

### Positive

- Testing reduces to comparing a return value against an expected one.
- A pure call can be moved, repeated, cached, or elided without changing
  behaviour, which frees both the compiler and the programmer.
- Reasoning stays local: a reader need not track what a call might have altered.
- Pure functions parallelise without synchronisation, because there is nothing
  to race over.

### Negative

- Purity is not expressible in C++ and not checkable, so this record is a
  discipline with no mechanical support at all; the upstream rule concedes as
  much.
- Passing everything a function needs as parameters lengthens parameter lists,
  which runs directly into the rule that argument counts stay low.
- Returning results instead of mutating in place copies data that could have
  been updated, which costs where the data is large and the loop is hot.
- An interactive program is impure by nature — input, rendering, file system —
  so the rule can only ever apply to a fraction of it, and the boundary is a
  judgement call.

### Neutral

- Purity is about observable behaviour, so caching inside a function does not
  make it impure as long as callers cannot tell.
- The rule expresses a preference, not a prohibition; impure functions remain
  necessary and normal.

## Enforcement

Not possible. Nothing in the language marks a function as pure, and nothing
checks the claim.

## Related

- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — the data-side
  statement of the same preference.
- [0021](0021-i-2-avoid-non-const-global-variables.md) — the hidden inputs that
  make purity impossible.
- [0439](0439-f-2-a-function-should-perform-a-single-logical-operation.md) —
  splitting computation from I/O is what leaves a pure half behind.
- [0020](0020-i-1-make-interfaces-explicit.md) — a pure function's interface is
  exactly its signature, which is the ideal that record describes.
