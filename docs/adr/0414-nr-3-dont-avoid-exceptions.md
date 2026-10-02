# 0414. NR.3: Don't avoid exceptions

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section NR (Non-Rules and myths), transcribed 2026-08-10

## Context

The NR section collects rules that are popular somewhere but that the
guidelines deliberately do not recommend. This record adopts the rejection, not
the practice being rejected.

The practice here is a blanket prohibition on exceptions. Four reasons are
usually given for one: that exceptions are inefficient, that they lead to leaks
and errors, that their performance is not predictable, and that the
exception-handling run-time support takes up too much space. The guidelines do
not claim to settle an argument that has run for forty years, and they
acknowledge that specialised applications exist — hard real-time systems without
support for reliable estimates of the cost of handling an exception — where
exceptions are indeed inappropriate. They answer each of the four objections in
turn.

*Exceptions are inefficient.* Compared to what? A comparison is only meaningful
if the same set of errors is handled, and handled equivalently; a program that
terminates immediately on seeing an error is not comparable with one that
carefully cleans up resources before logging it. Some systems do have poor
exception-handling implementations, and such an implementation can force another
approach, but that is not a fundamental problem with exceptions. Any efficiency
argument, in any context, needs data that actually gives insight into the
problem under discussion.

*Exceptions lead to leaks and errors.* They do not. A program that is a rat's
nest of pointers with no overall strategy for resource management has a problem
whatever it does about errors. If such a system runs to a million lines,
exceptions probably cannot be introduced into it — but that is a problem with
excessive and undisciplined pointer use, not with exceptions. RAII is what makes
exception-based error handling simple and safe, and in the guidelines' view
simpler and safer than the alternatives.

*Exception performance is not predictable.* In a hard real-time system that must
guarantee completion of a task within a given time, tools are needed to back up
that guarantee, and as far as the guidelines' authors know such tools are not
available to most programmers.

*The run-time support takes up too much space.* This can be the case in small,
usually embedded, systems. Before abandoning exceptions on that basis, consider
what consistent error handling by error code would require in space, and what
failing to catch an error would cost.

Many, possibly most, problems with exceptions stem from historical needs to
interact with messy old code. The fundamental arguments for using them are that
they clearly differentiate an erroneous return from an ordinary return, that
they cannot be forgotten or ignored, and that they can be used systematically.

Upstream closes with four reminders. Exceptions are for reporting errors — in
C++; other languages may put them to other uses. They are not for errors that
can be handled locally. One should not try to catch every exception in every
function, which is tedious, clumsy, and leads to slow code. And they are not for
errors that require instant termination of a module or system after a
non-recoverable failure. The alternatives upstream names are RAII and, for
contracts and assertions, `Expects` and `Ensures`.

Upstream marks this rule's example as a placeholder, so there is no code to
transcribe.

## Decision

Do not adopt a blanket rule against exceptions, and do not reject them on any of
the four grounds above.

- Use exceptions to report an error — a failure to perform a required task —
  and let them propagate to a level that can do something about the failure.
- Do not use them for conditions that can be handled locally, where an ordinary
  return value or a branch is the honest answer.
- Do not use them for a non-recoverable failure that requires immediate
  termination; terminate.
- Do not wrap every call in `try`/`catch`. Catch where the error can be
  handled, and nowhere else.
- Manage every resource with RAII, so that unwinding releases what it should
  without any cleanup code on the error path.
- Use `Expects` and `Ensures` for preconditions and postconditions. A contract
  violation is a programming error, not an exceptional condition to be reported.
- Do not reject exceptions on an efficiency or code-size claim that has not been
  measured, and do not accept a comparison in which the two alternatives handle
  different sets of errors.
- Reconsider only where the domain genuinely forbids them — a hard real-time
  system with no bound on the cost of handling a throw, or a target where the
  run-time support does not fit — and then say which of those conditions
  applies.

## Rationale

The sharpest of the guidelines' answers is the second: exceptions do not cause
leaks, undisciplined resource management does, and a codebase that leaks under
exceptions was already relying on every path being written out by hand. The
objection is really a report that RAII is missing.

The three properties exceptions are credited with — an error return that is
distinguishable from an ordinary one, that cannot be ignored, and that can be
applied systematically — are the requirements any error strategy has to meet.
The four objections are, at best, claims about a particular implementation on a
particular target, and each of them is measurable rather than a matter of
opinion, which is what makes the blanket prohibition a myth rather than a
trade-off.

## Consequences

### Positive

- The error path is not written out at every intermediate frame, so functions
  that cannot handle a failure do not have to mention it.
- A failure cannot be silently discarded, which an ignorable return value can be.
- The error channel is separate from the return type, so a function's ordinary
  result does not have to encode a verdict alongside it.
- Ruling out the four folk objections keeps the argument on measurable ground.

### Negative

- The rule depends entirely on RAII being used everywhere. In code that manages
  resources by hand, adopting exceptions makes things worse, and the guidelines
  concede that a large legacy codebase may not be able to adopt them at all.
- A thrown exception is invisible at the call site: nothing in a signature says
  which failures can emerge, so a caller cannot see from the interface what it
  must be prepared for.
- Constructors can report failure this way, but destructors and `noexcept`
  functions cannot, so the mechanism does not cover every path.
- The excluded domains are real. Hard real-time and tightly constrained embedded
  targets are told to use something else, and the rule offers no guidance on
  what, beyond noting that error codes cost space too.
- Exceptions do not cross a C ABI boundary, so any interface that must be
  callable from C needs a second error mechanism regardless.

### Neutral

- The rule concerns errors only. It is not an argument for using exceptions as a
  control-flow mechanism.
- Whether a particular implementation's exception handling is fast enough is an
  empirical question about a toolchain, not about the language feature.

## Related

- [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md)
  — the positive rule this non-rule protects.
- [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
  — RAII, which upstream names as the alternative that makes exceptions safe.
- [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md) — `Expects`,
  the other alternative offered, for the contract cases that are not errors.
- [0302](0302-e-17-dont-try-to-catch-every-exception-in-every-function.md) — the
  reminder that catching everywhere is tedious, clumsy and slow.
