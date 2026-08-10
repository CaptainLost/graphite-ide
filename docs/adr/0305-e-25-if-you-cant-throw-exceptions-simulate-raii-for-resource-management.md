# 0305. E.25: If you can't throw exceptions, simulate RAII for resource management

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

Even without exceptions, RAII is usually the best and most systematic way of
dealing with resources.

The rule is explicit that this is a fallback. Error handling using exceptions is
the only complete and systematic way of handling non-local errors in C++:
non-intrusively signalling failure to construct an object requires an exception,
and signalling errors in a way that cannot be ignored requires exceptions. If
you cannot use exceptions, simulate their use as best you can. A lot of fear of
exceptions is misguided — when used for exceptional circumstances in code that
is not littered with pointers and complicated control structures, exception
handling is almost always affordable in time and space and almost always leads
to better code, assuming a good implementation of the mechanism, which is not
available on all systems. The cases the rule accepts are hard real-time systems,
where an operation must complete within a fixed time and appropriate time
estimation tools are unavailable — such systems typically also ban dynamic
memory — and bodies of old-style code with ill-defined ownership where
introducing systematic exception handling is infeasible. Before condemning
exceptions, consider the cost and complexity of error codes; and if performance
is the worry, measure.

The technique itself is to give a resource type a `valid()` member and check it
after construction. Suppose you wanted to write:

```cpp
void func(zstring arg)
{
    Gadget g {arg};
    // ...
}
```

If the gadget is not correctly constructed, `func` exits with an exception. If
an exception cannot be thrown, the RAII style can be simulated by adding a
`valid()` member function to `Gadget`:

```cpp
error_indicator func(zstring arg)
{
    Gadget g {arg};
    if (!g.valid()) return gadget_construction_error;
    // ...
    return 0;   // zero indicates "good"
}
```

The problem, of course, is that the caller now has to remember to test the
return value. To encourage doing so, consider adding `[[nodiscard]]`.

Note what is and is not being simulated. The destructor half of RAII works
unchanged without exceptions — `g`'s destructor still runs on every exit from
`func`, so nothing leaks. What needs simulating is only the constructor's
ability to refuse: instead of the object never existing, it exists in a state
the caller must detect.

## Decision

Where exceptions cannot be thrown, keep RAII and simulate the part of it that
the throw provided.

- Continue to give every resource an owner whose destructor releases it. Not
  throwing changes nothing about this half of RAII.
- Add a `valid()` operation to every resource handle whose construction can
  fail, and check it immediately after construction, before any other use.
  Treat the check as part of the construction.
- Return the failure onwards rather than swallowing it, as `func` returns
  `gadget_construction_error` above.
- Mark the fallible return `[[nodiscard]]`, which is what the rule suggests to
  encourage the caller to test it.
- Ensure the destructor of a partially constructed object is still correct: it
  must release what was acquired and nothing else.
- Do not conclude that being unable to throw means abandoning systematic
  resource management. The two are separate questions, and only one of them is
  settled by the constraint.

## Rationale

The half of RAII that depends on exceptions is smaller than it first appears.
Destructors run on every scope exit regardless of what caused it, so leak
prevention is untouched; what needs replacing is only the constructor's ability
to refuse, and `valid()` replaces it — imperfectly, because the object exists
either way.

`[[nodiscard]]` is the remedy the rule itself offers, and it is what makes the
substitute nearly honest. It turns "the caller has to remember" into a
diagnostic, which is as close as a return value gets to the property an
exception has for free: that ignoring it is not something you can do by writing
nothing.

## Consequences

### Positive

- Leak prevention survives intact, because destructors do not depend on the
  error mechanism.
- The technique is systematic: every resource handle answers the same question
  the same way, so a missing check is visible as an anomaly.
- `[[nodiscard]]` gives the compiler something to say when a construction
  failure is dropped.

### Negative

- It increases the size of the code. Every fallible construction becomes a
  declaration plus a check, where the exception version was a declaration.
- There is no implicit propagation. A failure four frames down is threaded back
  through four signatures by hand, and each frame must remember to do it.
- The `valid()` check can simply be forgotten, and the object will look
  constructed. `[[nodiscard]]` mitigates the returned indicator but not the
  handle's own state.
- The type now has two states — constructed-and-usable and
  constructed-and-not — which is exactly the two-state object
  [0293](0293-e-5-let-a-constructor-establish-an-invariant-and-throw-if-it-cannot.md)
  exists to forbid. Every member function must decide what it does in the
  second.
- It requires modifying the resource types. `valid()` has to be added to
  `Gadget`, which means a codebase taking this route needs its own vocabulary of
  handles rather than the standard ones, since no standard type offers it.

### Neutral

- The rule regards this as second best and says so. It is a fallback for a
  constrained environment, not a recommendation.
- The technique says nothing about how the failure travels once detected; that
  is [0307](0307-e-27-if-you-cant-throw-exceptions-use-error-codes-systematically.md).

## Enforcement

- Possible only for specific versions of this idea: for example, test for a
  systematic check of `valid()` after resource handle construction.

## Related

- [0294](0294-e-6-use-raii-to-prevent-leaks.md) — the RAII being simulated.
- [0293](0293-e-5-let-a-constructor-establish-an-invariant-and-throw-if-it-cannot.md)
  — the constructor obligation this approximates when the throw is unavailable.
- [0307](0307-e-27-if-you-cant-throw-exceptions-use-error-codes-systematically.md)
  — the companion rule for transmitting the failure outwards.
