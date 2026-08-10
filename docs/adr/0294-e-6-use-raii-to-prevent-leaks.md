# 0294. E.6: Use RAII to prevent leaks

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

Leaks are typically unacceptable, and manual resource release is error-prone.
RAII — Resource Acquisition Is Initialisation — is the simplest, most systematic
way of preventing leaks.

```cpp
void f1(int i)   // Bad: possible leak
{
    int* p = new int[12];
    // ...
    if (i < 17) throw Bad{"in f()", i};
    // ...
}
```

The resource could be released carefully before the throw:

```cpp
void f2(int i)   // Clumsy and error-prone: explicit release
{
    int* p = new int[12];
    // ...
    if (i < 17) {
        delete[] p;
        throw Bad{"in f()", i};
    }
    // ...
}
```

This is verbose, and in larger code with multiple possible exits the explicit
releases become repetitive and error-prone. A handle removes them:

```cpp
void f3(int i)   // OK: resource management done by a handle
{
    auto p = make_unique<int[]>(12);
    // ...
    if (i < 17) throw Bad{"in f()", i};
    // ...
}
```

This works even when the exit is implicit because it happened in a called
function:

```cpp
void f4(int i)   // OK: resource management done by a handle
{
    auto p = make_unique<int[]>(12);
    // ...
    helper(i);   // might throw
    // ...
}
```

Unless pointer semantics are really needed, use a local resource object, which
is simpler, safer, and often more efficient:

```cpp
void f5(int i)   // OK: resource management done by local object
{
    vector<int> v(12);
    // ...
    helper(i);   // might throw
    // ...
}
```

If there is no obvious resource handle and defining a proper RAII object is
infeasible, cleanup actions can be represented by a `final_action` object as a
last resort.

Upstream then asks what to do when exceptions cannot be used, and challenges the
assumption first: the only good reasons it recognises are a system so small that
exception support would consume most of the available memory, a hard real-time
system without tools guaranteeing an exception is handled within the required
time, a body of legacy code using pointers in ways that make ownership
unrecognisable so that exceptions could cause leaks, and an unreasonably poor
implementation of the exception mechanism. Only the first is fundamental. Where
exceptions cannot be used, RAII should be simulated: systematically check that
objects are valid after construction, and still release all resources in the
destructor. One strategy is to add a `valid()` operation to every resource
handle:

```cpp
void f()
{
    vector<string> vs(100);   // not std::vector: valid() added
    if (!vs.valid()) {
        // handle error or exit
    }
    ifstream fs("foo");       // not std::ifstream: valid() added
    if (!fs.valid()) {
        // handle error or exit
    }
    // ...
} // destructors clean up as usual
```

That increases the size of the code, does not allow for implicit propagation of
failures, and the `valid()` checks can be forgotten.

RAII is independent of which mechanism reports failure. A destructor runs on
every exit from a scope, whether the exit is a throw, an early return, or
falling off the end, so the guarantee it provides does not depend on exceptions
being the error channel. That is why the rule survives into the "if you can't
throw exceptions" rules later in this section rather than being abandoned with
the throw.

## Decision

Give every resource an owner whose destructor releases it.

- Acquire a resource in the constructor of an object whose destructor releases
  it. Do not pair a raw acquisition with a manual release.
- Prefer a local value — a `vector`, a `string`, a container — to a smart
  pointer, and a smart pointer to a raw owning pointer.
- Never leave the result of a bare `new` or a bare `malloc` in a raw local. Hand
  it to an owner on the line that acquires it.
- Where no suitable handle exists and writing one is not practical, use a
  `final_action`/`finally` object rather than duplicating cleanup on every exit.
- Apply this to non-memory resources too: file handles, process handles, locks,
  and anything else with a paired acquire and release.
- Where exceptions are genuinely unavailable, keep RAII and simulate the part of
  it that the throw provided; see
  [0305](0305-e-25-if-you-cant-throw-exceptions-simulate-raii-for-resource-management.md).

## Rationale

The argument is about counting. `f2` is correct only if every exit path releases
`p`, and the number of exit paths is not fixed by the function's own text —
`helper(i)` adds one that is invisible. RAII makes correctness independent of
that count.

The second argument is that manual release is not merely error-prone but
degrades under maintenance: adding an early return to `f2` a year later is a
one-line change that introduces a leak, and nothing about the line looks
dangerous. `f5` cannot be broken that way.

## Consequences

### Positive

- Correctness on the failure path stops depending on enumerating the exit paths.
- Adding an early return, or a call that can fail, cannot introduce a leak.
- Cleanup order follows construction order in reverse, which is defined and
  local, rather than being maintained by hand.

### Negative

- It only works for resources whose release is a single, non-failing action.
  Anything whose teardown can itself fail sits badly, because a destructor must
  not fail.
- Ownership must actually be decided. Shared or ambiguous ownership pushes
  towards `shared_ptr`, whose cost is real and whose cycles leak silently.
- Wrapping an existing C API means writing and testing a handle type that adds
  no functionality, which is friction at exactly the boundaries where the API is
  least pleasant.
- `final_action` is ad hoc: it is a lambda in a local variable that a later
  edit can move, shadow, or drop, and the compiler will not object.

### Neutral

- The rule does not care which handle is used, only that one exists.
- It is independent of the error-reporting mechanism. Destructors run on every
  exit, so the guarantee holds whether the exit is a throw or a return.
- The four reasons the guidelines accept for not using exceptions — a system too
  small to carry the support, a hard real-time deadline without the tooling to
  bound recovery time, legacy code with unrecognisable ownership, and a poor
  implementation — are reasons to simulate RAII, not to abandon it.

## Related

- [0014](0014-p-8-do-not-leak-any-resources.md) — the philosophy rule this
  implements.
- [0298](0298-e-13-never-throw-while-being-the-direct-owner-of-an-object.md) —
  the failure mode RAII exists to remove.
- [0304](0304-e-19-use-a-final-action-object-to-express-cleanup-if-no-suitable-resource-handle-is-available.md)
  — the last resort when no handle fits.
- [0305](0305-e-25-if-you-cant-throw-exceptions-simulate-raii-for-resource-management.md)
  — RAII where the failure channel is a return value.
