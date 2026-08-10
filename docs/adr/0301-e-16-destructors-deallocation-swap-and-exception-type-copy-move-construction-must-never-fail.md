# 0301. E.16: Destructors, deallocation, swap, and exception type copy/move construction must never fail

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

We do not know how to write reliable programs if a destructor, a `swap`, a
memory deallocation, or an attempt to copy or move-construct an exception object
fails — that is, if it exits by an exception or simply does not perform its
required action.

```cpp
class Connection {
    // ...
public:
    ~Connection()   // Don't: very bad destructor
    {
        if (cannot_disconnect()) throw I_give_up{information};
        // ...
    }
};
```

Many have tried to write reliable code violating this rule, for cases such as a
network connection that "refuses to close". To the best of the guidelines'
knowledge nobody has found a general way of doing this. Occasionally, for very
specific cases, you can get away with setting some state for future cleanup —
for example putting a socket that does not want to close on a "bad socket" list
to be examined by a regular sweep of the system state. Every example the authors
have seen of this is error-prone, specialised, and often buggy.

The standard library assumes that destructors, deallocation functions such as
`operator delete`, and `swap` do not throw. If they do, basic standard-library
invariants are broken.

The specific obligations are: deallocation functions, including `operator
delete`, must be `noexcept`; `swap` functions must be `noexcept`; most
destructors are implicitly `noexcept` by default; move operations should be made
`noexcept` as well. If writing a type intended to be used as an exception type,
ensure its copy constructor cannot fail. In general this cannot be enforced
mechanically, because there is no way to know whether a type is intended to be
used as an exception type. Similarly, try not to throw a type whose copy
constructor can fail — and again this cannot be enforced mechanically, because
even `throw std::string(...)` could throw in principle but does not in practice.

The reason these four operations in particular are singled out is that they are
the operations error recovery itself depends on. A destructor runs *while* an
error is being propagated; a failing one during unwinding calls
`std::terminate`. A `swap` is the standard way to implement a strong exception
guarantee, so if `swap` can fail there is no rollback. Deallocation is the last
step of releasing anything. And an exception object that cannot be copied cannot
be propagated. Each is a component of the recovery machinery, and machinery that
can fail while recovering from a failure has nowhere left to go.

The constraint is absolute rather than a matter of mechanism. A destructor has
no return value, so a resource whose release can fail has no channel at all —
which is why release-failure must be handled by policy, such as logging and
continuing, rather than by reporting.

## Decision

Make these operations incapable of failing.

- Do not throw from a destructor, and do not let an exception escape one. Most
  destructors are implicitly `noexcept`; do not defeat that.
- Mark deallocation functions, including any `operator delete`, `noexcept`.
- Mark `swap` `noexcept`, and implement it as member-wise exchange of things
  that cannot fail.
- Mark move construction and move assignment `noexcept`.
- Ensure a type intended to be thrown can be copied without failing; avoid
  throwing types whose copy can fail.
- Where a release genuinely can fail — closing a handle, flushing a file — do it
  in an explicit function that can report, and let the destructor be the
  best-effort fallback that logs and continues rather than propagating.
- Do not invent a deferred-cleanup list to work around this rule without a
  specific reason; the guidelines report every such scheme they have seen as
  error-prone, specialised, and often buggy.

## Rationale

These are the operations that run on the failure path, so allowing them to fail
means allowing failure handling to fail, and the language has no answer for the
second failure. The destructor case is the concrete form: during unwinding there
is already an exception in flight, and a second one terminates the process.

The `swap` case is the one most often overlooked and is what makes the strong
guarantee possible at all. Build a new state, then exchange: the exchange is the
step that must not fail, because it is the step with no earlier state to return
to. A `swap` that can fail leaves the object between two valid states, which is
the one place [0292](0292-e-4-design-your-error-handling-strategy-around-invariants.md)
says an object must never be.

## Consequences

### Positive

- Unwinding cannot itself fail, so an exception in flight reaches its handler
  rather than terminating the process on the way.
- A strong guarantee becomes implementable, because build-then-swap has a step
  that cannot go wrong.
- Standard-library invariants that assume non-throwing destructors, `swap`, and
  deallocation are actually satisfied.

### Negative

- Some resources genuinely can fail to release, and the rule does not make that
  go away — it relocates it. A buffered file whose final flush fails will have
  its failure swallowed by the destructor unless the caller remembered to close
  explicitly.
- Requiring an explicit fallible `Close()` alongside the destructor means two
  ways to release one resource, and callers who use only the second get the
  silent path.
- "Ensure an exception type's copy constructor cannot fail" is unenforceable, as
  the guidelines concede twice: nothing knows which types are exception types,
  and nothing can prove a copy will not throw — even `throw std::string(...)`
  could throw in principle, though it does not in practice.
- Marking a move `noexcept` is a promise about every member, including ones
  added later by someone who does not read the marking.

### Neutral

- Most destructors already satisfy the rule by default; the work is in the
  minority that release something interesting.
- The deferred-cleanup approach is not forbidden outright, only reported as
  consistently bad in practice.

## Enforcement

- Catch destructors, deallocation operations, and swaps that throw.
- Catch such operations that are not marked `noexcept`.

## Related

- [0297](0297-e-12-use-noexcept-when-exiting-a-function-because-of-a-throw-is-impossible-or-unacceptable.md)
  — the annotation this rule requires on those operations.
- [0294](0294-e-6-use-raii-to-prevent-leaks.md) — RAII depends on destructors
  that cannot fail.
- [0292](0292-e-4-design-your-error-handling-strategy-around-invariants.md) — why
  a failing `swap` is fatal to the strong guarantee.
