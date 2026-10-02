# 0309. E.30: Don't use exception specifications

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

Exception specifications make error handling brittle, impose a run-time cost,
and have been removed from the C++ standard.

```cpp
int use(int arg)
    throw(X, Y)
{
    // ...
    auto x = f(arg);
    // ...
}
```

If `f()` throws an exception other than `X` or `Y`, the unexpected handler is
invoked, which by default terminates. That is acceptable in itself. But suppose
we have checked that this cannot happen, and then `f` is changed to throw a new
exception `Z`: we now have a crash on our hands unless we change `use()` and
re-test everything. The snag is that `f()` might be in a library we do not
control, and the new exception is not anything `use()` can do anything about or
is in any way interested in.

We can change `use()` to pass `Z` through, but now `use()`'s callers probably
need to be modified. This quickly becomes unmanageable. Alternatively we can add
a `try`/`catch` to `use()` to map `Z` into an acceptable exception; that too
quickly becomes unmanageable. Changes to the set of exceptions often happen at
the lowest level of a system — because of a change to a network library or some
middleware — so the changes bubble up through long call chains. In a large code
base that could mean nobody can update to a new version of a library until the
last user has been modified. And if `use()` is part of a library, it might not
be possible to update it at all, because the change could affect unknown
clients.

The guidelines are explicit that this would not have been any better had
exception specifications been statically enforced. The policy of letting
exceptions propagate until they reach a function that can potentially handle
them has proven itself over the years. Dynamic exception specifications were
deprecated in C++11 and removed in C++17, so in a modern dialect the syntax does
not compile.

If no exception can be thrown, use `noexcept` instead; see
[0297](0297-e-12-use-noexcept-when-exiting-a-function-because-of-a-throw-is-impossible-or-unacceptable.md).
That is the surviving fragment of the idea, and it survives because it is a
single bit rather than a list: it makes a claim that is local to the function
and needs no maintenance when a callee's error set changes.

What remains worth recording is the reasoning, because it applies to any attempt
to enumerate a function's possible failures in its signature — including a
return-based error type whose set of failures is narrowed function by function.

## Decision

Do not write dynamic exception specifications, and do not reintroduce the idea
by other means.

- Never write `throw(X, Y)` on a function.
- Use `noexcept` where a throw cannot escape or must not, and nothing where it
  can.
- Do not enumerate a function's possible failures in its signature in a way that
  forces every caller up the chain to be edited when a low-level component gains
  a new failure mode.
- Let a failure propagate to a frame that can act on it rather than mapping it
  into a locally acceptable form at each level.

## Rationale

The defect is that the specification is a claim about the transitive behaviour
of everything the function calls, written in a place that only knows about the
function itself. It is therefore invalidated by changes the author cannot see,
in code the author does not own, and the invalidation is discovered at run time
by terminating.

`noexcept` avoids this by being a promise rather than an enumeration. "Nothing
escapes here" is enforceable at the boundary — the function can be written to
guarantee it — whereas "only `X` and `Y` escape" depends on a set that every
dependency can extend.

## Consequences

### Positive

- Adding a failure mode to a low-level component does not force edits up an
  entire call chain.
- A library can be updated without waiting for its last client to be modified.
- No run-time cost is paid for a check that terminates when the specification
  turns out to be stale.

### Negative

- Callers lose all static information about what can escape a function, beyond
  the binary `noexcept`. Documentation is the only remaining channel, and it is
  unchecked.
- The same argument tells against precise per-function error sets in any
  mechanism, so the alternative is deliberately coarse: whatever a function can
  fail with, its declaration does not say.
- Nothing prevents a function from silently acquiring a new failure mode; the
  cost avoided here is paid as a loss of interface precision.
- Since C++17 the rule is enforced by the compiler, so recording it has value
  only for the reasoning, not for the prohibition.

### Neutral

- `noexcept` is not an exception specification in the sense this rule prohibits,
  and is recommended.
- The rule is moot as a practical matter in C++17 and later, where the syntax
  does not compile; it remains live wherever an older dialect is still in use,
  and the reasoning outlives the syntax either way.

## Enforcement

- Flag every exception specification.

## Related

- [0297](0297-e-12-use-noexcept-when-exiting-a-function-because-of-a-throw-is-impossible-or-unacceptable.md)
  — what to use instead.
- [0302](0302-e-17-dont-try-to-catch-every-exception-in-every-function.md) — the
  propagation policy this rule endorses.
