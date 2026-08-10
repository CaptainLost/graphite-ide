# 0059. C.36: A destructor must not fail

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

In general we do not know how to write error-free code if a destructor can fail.
The standard library requires that all classes it deals with have destructors
that do not exit by throwing.

```cpp
class X {
public:
    ~X() noexcept;
    // ...
};

X::~X() noexcept
{
    // ...
    if (cannot_release_a_resource) terminate();
    // ...
}
```

Many have tried to devise a fool-proof scheme for dealing with failure in
destructors, and none has succeeded in producing a general one. This can be a
real practical problem: consider a socket that will not close. The writer of a
destructor does not know why the destructor is being called and cannot refuse to
act by throwing an exception. Making it worse, many close and release operations
are not retryable. If at all possible, treat a failure to close or clean up as a
fundamental design error and terminate.

Declare a destructor `noexcept`. That ensures it either completes normally or
terminates the program.

If a resource cannot be released and the program must not fail, try to signal
the failure to the rest of the system somehow, possibly even by modifying some
global state in the hope that something will notice and be able to deal with it.
Be fully aware that this technique is special-purpose and error-prone. Consider
the connection that will not close: probably there is a problem at the other end
of the connection, and only code responsible for both ends can properly handle
it. The destructor could send a message to the responsible part of the system,
consider the connection closed, and return normally.

If a destructor uses operations that could fail, it can catch exceptions and in
some cases still complete successfully, for example by using a different cleanup
mechanism from the one that threw.

## Decision

Write destructors so that they cannot report a failure, because there is nobody
to report it to.

- Never let an exception escape a destructor. Declare destructors `noexcept`,
  per [0060](0060-c-37-make-destructors-noexcept.md), so that an escaping
  exception terminates rather than propagating.
- If a destructor calls something that can fail, catch the failure inside the
  destructor and either recover by another route or accept the loss.
- Where a resource genuinely cannot be released, treat it as a design error and
  terminate, unless the program has an explicit, documented reason not to.
- Where terminating is not acceptable, record the failure through a channel the
  rest of the system already monitors — a log, a status flag consulted
  elsewhere — and return normally. Treat that as a special-purpose measure, not
  a pattern.
- Do not try to report the failure by returning either. A destructor has no
  return value, so a release that can genuinely fail must be exposed as an
  ordinary member function that the caller invokes and checks before the object
  is destroyed.

## Rationale

The reason a destructor cannot fail is not that failure is unlikely but that
there is no correct recipient for the report. The destructor may be running
during stack unwinding from another failure, in which case throwing terminates;
it may be running at the end of a scope the caller never wrote; and in neither
case does the caller have a decision left to make.

Reporting by return value is no help either, since a destructor returns
nothing; there is no channel of any kind out of it. A class whose release can
genuinely fail therefore needs an explicit `Close()` or `Flush()` that the
caller invokes and checks, with the destructor left as a last-resort best-effort
cleanup — the same shape the standard library uses for streams.

## Consequences

### Positive

- Stack unwinding is safe: no destructor can turn one failure into a
  termination through a second exception.
- An RAII type written under this record is usable in a container and in the
  standard library, both of which require non-throwing destruction.
- The fallible part of a cleanup is forced out into a named function where its
  result can be checked.

### Negative

- Real cleanup failures are discarded. The rule does not solve the problem, it
  decides who loses, and what is lost may be data — an unflushed buffer, an
  uncommitted transaction.
- Requiring an explicit `Close()` for anything that can fail reintroduces
  two-phase teardown, which is the mirror image of the two-phase initialisation
  that [0062](0062-c-41-a-constructor-should-create-a-fully-initialized-object.md)
  rejects, and it is just as easy for a caller to forget.
- "Terminate on cleanup failure" is a severe response to a condition that is
  often transient and external, and it is not obviously right for a desktop
  application that could otherwise keep running.

### Neutral

- The rule constrains how a destructor may end, not what it does.
- Catching and handling an exception inside a destructor is permitted and
  sometimes the correct answer.

## Enforcement

- (Simple) A destructor should be declared `noexcept` if it could throw.

## Related

- [0060](0060-c-37-make-destructors-noexcept.md) — the mechanical form of this
  rule.
- [0055](0055-c-31-all-resources-acquired-by-a-class-must-be-released-by-the-classs-destructor.md)
  — the obligation that creates the fallible release in the first place.
- [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md)
  — the general position on reporting failure, and its stated gap for
  constructors and operators.
