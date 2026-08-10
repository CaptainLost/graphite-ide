# 0307. E.27: If you can't throw exceptions, use error codes systematically

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

Systematic use of any error-handling strategy minimises the chance of forgetting
to handle an error. Three questions have to be answered: how an error indicator
is transmitted out of a function, how all resources are released before an error
exit, and what is used as the error indicator.

In general, returning an error indicator implies returning two values — the
result and an indicator. The indicator can be part of the object, so that an
object carries a `valid()` test, or a pair of values can be returned:

```cpp
Gadget make_gadget(int n)
{
    // ...
}

void user()
{
    Gadget g = make_gadget(17);
    if (!g.valid()) {
            // error handling
    }
    // ...
}
```

That approach fits with simulated RAII resource management, and `valid()` can
return a value from an error-indicator enumeration. Where the type cannot or
should not be modified, a pair must be returned instead:

```cpp
std::pair<Gadget, error_indicator> make_gadget(int n)
{
    // ...
}

void user()
{
    auto r = make_gadget(17);
    if (!r.second) {
            // error handling
    }
    Gadget& g = r.first;
    // ...
}
```

Some people prefer a specific type, so that the members have names rather than
`first` and `second` and so that there is no confusion with other uses of
`std::pair`:

```cpp
Gval make_gadget(int n)
{
    // ...
}

void user()
{
    auto r = make_gadget(17);
    if (!r.err) {
            // error handling
    }
    Gadget& g = r.val;
    // ...
}
```

The harder half is cleanup. In general you must clean up before an error exit,
and this can be messy:

```cpp
std::pair<int, error_indicator> user()
{
    Gadget g1 = make_gadget(17);
    if (!g1.valid()) {
        return {0, g1_error};
    }
    Gadget g2 = make_gadget(31);
    if (!g2.valid()) {
        cleanup(g1);
        return {0, g2_error};
    }
    // ...
    if (all_foobar(g1, g2)) {
        cleanup(g2);
        cleanup(g1);
        return {0, foobar_error};
    }
    // ...
    cleanup(g2);
    cleanup(g1);
    return {res, 0};
}
```

Simulating RAII can be non-trivial, especially in functions with multiple
resources and multiple possible errors. A not uncommon technique is to gather
the cleanup at the end of the function to avoid the repetition, at the cost of
`goto` and an extra scope needed to make it compile:

```cpp
std::pair<int, error_indicator> user()
{
    error_indicator err = 0;
    int res = 0;
    Gadget g1 = make_gadget(17);
    if (!g1.valid()) {
        err = g1_error;
        goto g1_exit;
    }
    {
        Gadget g2 = make_gadget(31);
        if (!g2.valid()) {
            err = g2_error;
            goto g2_exit;
        }
        if (all_foobar(g1, g2)) {
            err = foobar_error;
            goto g2_exit;
        }
        // ...
    g2_exit:
        if (g2.valid()) cleanup(g2);
    }
g1_exit:
    if (g1.valid()) cleanup(g1);
    return {res, err};
}
```

The larger the function, the more tempting this technique becomes, and `finally`
can ease the pain a little. The larger the program becomes, the harder it is to
apply an error-indicator strategy systematically. The rule's own conclusion is
that exception-based error handling is preferable, and that functions should be
kept short.

Both `user()` examples are made unnecessary by the point they are illustrating.
Neither `Gadget` is a proper RAII type — which is why the `cleanup()` calls are
there at all — and if they were, every cleanup line in both functions would
disappear and only the checks would remain. The `goto` maze is a consequence of
the resource handling, not of the error indicator.

## Decision

Where exceptions cannot be thrown, use one error-indicator mechanism everywhere,
and make ignoring it as hard as possible.

- Answer the three questions once, for the whole program: how an indicator
  leaves a function, how resources are released before an error exit, and what
  the indicator is. Do not answer them differently per module.
- Prefer a purpose-built type with named members to `std::pair`, so that call
  sites read `r.err` and `r.val` rather than `r.second` and `r.first`, and so
  that the type is not confused with other uses of `std::pair`.
- Where the type can be modified, a `valid()` member on the object itself is the
  form that composes with simulated RAII; see
  [0305](0305-e-25-if-you-cant-throw-exceptions-simulate-raii-for-resource-management.md).
- Mark the indicator `[[nodiscard]]`. A returned failure that can be dropped by
  writing nothing will be.
- Solve the cleanup half with RAII rather than with `cleanup()` calls before
  each return. A type that releases itself removes every cleanup line from the
  examples above.
- Do not reach for `goto`-to-label cleanup chains. Where that shape is tempting,
  the resources are not owned properly; see
  [0304](0304-e-19-use-a-final-action-object-to-express-cleanup-if-no-suitable-resource-handle-is-available.md).
- Keep functions short. The technique degrades with function length faster than
  exceptions do, which is the rule's own observation.

## Rationale

The word carrying the rule is "systematically". A single mechanism used
everywhere means every call site looks the same, so a missing check is visible
as an anomaly; a mixture means nothing looks anomalous, and the checks that were
never written are indistinguishable from the calls that never needed one.

The second point is that error codes and RAII are separable, and conflating them
is what makes the examples so unpleasant. All the ugliness in `user()` comes
from `cleanup()`, not from the indicator. Keep RAII and the remaining cost is a
check per fallible call — which is real, but is not the `goto` maze.

## Consequences

### Positive

- Every fallible call has the same shape, so an unchecked one stands out.
- The failure is part of the signature, so a caller can see from the declaration
  that a call can fail — which an exception does not provide.
- It works where exceptions are unavailable: across a C ABI, under a hard
  real-time deadline, and on targets without the runtime support.
- `[[nodiscard]]` gives the compiler grounds to complain about a dropped
  indicator.

### Negative

- There is no implicit propagation. Every intermediate frame checks and
  forwards, so a five-call chain is five checking blocks, and the checking code
  can outnumber the working code.
- It scales badly with program size, as the rule says outright: the larger the
  program, the harder the strategy is to apply consistently, and there is no
  point at which it gets easier.
- Constructors, destructors, and operators have no return value to carry an
  indicator, so the mechanism does not cover every fallible operation.
- Returning two values means the value's validity is conditional on the
  indicator, and nothing in the language stops a caller reading the value
  without checking.
- The cleanup half is genuinely hard, as the second `user()` shows: multiple
  resources and multiple failure points produce either repeated cleanup or a
  `goto` structure whose label order has to be maintained by hand.

### Neutral

- The three forms described — `valid()` on the object, a `std::pair`, and a
  purpose-built type — differ mainly in ergonomics, and the rule expresses a
  preference among them rather than a requirement.
- The rule prefers exceptions and says so; it applies only where they are
  unavailable.

## Enforcement

- Awkward. There is no general mechanical check for systematic use of an error
  indicator, beyond what `[[nodiscard]]` provides on the individual type.

## Related

- [0305](0305-e-25-if-you-cant-throw-exceptions-simulate-raii-for-resource-management.md)
  — the cleanup half, without which these examples are unmanageable.
- [0290](0290-e-2-throw-an-exception-to-signal-that-a-function-cant-perform-its-assigned-task.md)
  — the mechanism this replaces, and which the guidelines prefer.
- [0308](0308-e-28-avoid-error-handling-based-on-global-state-e-g-errno.md) — the
  error-code style this rule rules out.
