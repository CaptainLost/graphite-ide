# 0306. E.26: If you can't throw exceptions, consider failing fast

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

If you cannot do a good job at recovering, at least you can get out before too
much consequential damage is done.

If you cannot be systematic about error handling, consider "crashing" as a
response to any error that cannot be handled locally. That is, if you cannot
recover from an error in the context of the function that detected it, call
`abort()`, `quick_exit()`, or a similar function that will trigger some sort of
system restart. In systems with many processes or many computers you need to
expect and handle fatal crashes anyway, say from hardware failures, so
"crashing" is simply leaving error handling to the next level of the system.

```cpp
void f(int n)
{
    // ...
    p = static_cast<X*>(malloc(n * sizeof(X)));
    if (!p) abort();     // abort if memory is exhausted
    // ...
}
```

Most programs cannot handle memory exhaustion gracefully anyway. This is roughly
equivalent to:

```cpp
void f(int n)
{
    // ...
    p = new X[n];    // throw if memory is exhausted (by default, terminate)
    // ...
}
```

Typically it is a good idea to log the reason for the "crash" before exiting.

The comparison between the two snippets is the substance of the rule. The second
looks like it recovers and the first does not, but with no handler installed
both terminate; the difference is that the first says so. Failing fast is
therefore not a lesser form of error handling — it is a decision, taken
deliberately, that a particular condition will not be recovered from, made
visible at the point of detection rather than deferred to a handler that does
not exist.

Two limits apply. Failing fast is right for conditions that indicate the program
is already wrong or cannot proceed at all — a broken invariant, a violated
precondition, an exhausted allocator. It is wrong for conditions the environment
routinely produces, such as a file that does not exist, because those are
outcomes the program was built to deal with. And it is a poor fit for a program
holding state that nothing else can reconstruct, where the cost of the crash
falls on whoever loses that state rather than on a supervisor that will restart
the process. The rule's own justification — that in a system of many processes
and many computers fatal crashes must be expected anyway — is an argument about
that supervisor's existence.

## Decision

Where exceptions cannot be thrown, fail fast on conditions that will not be
recovered from, and say so where they are detected.

- Decide explicitly which conditions are unrecoverable. Treat a broken invariant
  or a violated precondition as one; treat an outcome the environment routinely
  produces as an error to be reported.
- Call `abort()`, `quick_exit()`, or an equivalent that triggers a system
  restart, at the point where an error is detected that cannot be handled
  locally.
- Log the reason before exiting. An unlogged abort is indistinguishable from a
  hardware fault.
- Do not write a recovery path you cannot test and do not intend to exercise.
  A handler that logs and continues in an unrecoverable state is worse than a
  clean exit, because it produces damage after the point of failure.
- Weigh what the process holds before aborting it. Failing fast is cheapest
  where a supervisor restarts the process, and most expensive where the process
  is the only thing holding state that matters.

## Rationale

The argument is about consequential damage, not about tidiness. Continuing past
an unrecoverable failure means every subsequent action operates on state that is
already wrong, and any of those actions may make the wrongness durable by
writing it somewhere. Stopping bounds the damage to what has already happened.

The second argument is honesty about what the alternative achieves. A program
with no handler for memory exhaustion terminates whether or not it throws; the
`abort()` form differs only in stating the policy at the point of detection,
where a reader will see it, instead of leaving it to be inferred from the
absence of a handler somewhere else.

## Consequences

### Positive

- Damage is bounded at the point of detection rather than propagating through
  subsequent operations.
- The decision not to recover is visible in the code, rather than implied by a
  missing handler.
- No unreachable, untested recovery paths are written for conditions that will
  never be recovered from.

### Negative

- Aborting destroys whatever the process was holding and had not yet made
  durable, which is a real cost wherever no supervisor exists to make it good.
  The supervisor-restart argument covers the systems that have one.
- The classification is a judgement made in advance, and a condition classified
  as fatal turns out to be recoverable more often than one expects.
- Where the check is an assertion, it is conventionally compiled out in release
  builds, so the fail-fast behaviour may be present exactly where it is cheap to
  have and absent where it matters.
- Logging before exiting is itself work that can fail, and the state in which it
  runs is by definition a state the program has declared broken.

### Neutral

- The rule says "consider", not "always"; it competes with recovery rather than
  replacing it.
- The `abort()`-versus-`new` comparison shows the rule is often about making an
  existing behaviour explicit rather than changing it.

## Enforcement

- Awkward. No mechanical check is offered, and none is obvious: whether a
  condition should have been fatal is a design judgement, not a property of the
  code.

## Related

- [0297](0297-e-12-use-noexcept-when-exiting-a-function-because-of-a-throw-is-impossible-or-unacceptable.md)
  — `noexcept` as the declarative form of the same decision.
- [0305](0305-e-25-if-you-cant-throw-exceptions-simulate-raii-for-resource-management.md)
  — the alternative for conditions that are worth recovering from.
- [0013](0013-p-7-catch-run-time-errors-early.md) — detecting early is what makes
  failing fast bound the damage.
