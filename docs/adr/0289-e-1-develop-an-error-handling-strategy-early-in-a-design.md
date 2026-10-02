# 0289. E.1: Develop an error-handling strategy early in a design

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

A consistent and complete strategy for handling errors and resource leaks is
hard to retrofit into a system.

Error handling is four jobs, not one: detecting an error, transmitting
information about it to some handler, preserving a valid state of the program,
and avoiding resource leaks. A strategy that answers only the first of those is
not a strategy. It is not possible to recover from all errors, and where
recovery is impossible what matters is getting out quickly in a well-defined
way.

The strategy must also be simple, or it becomes a source of even worse errors.
Error-handling code is the code that is least often executed and least often
tested, which is why it is a disproportionate source of bugs — a mechanism that
is elaborate enough to be interesting is a mechanism whose failure paths nobody
has run.

The rules that follow are aimed at several kinds of error: type violations such
as the misuse of unions and casts, resource leaks including memory leaks,
bounds errors, lifetime errors such as accessing an object after it has been
deleted, complexity errors made likely by overly complex expression of ideas,
and interface errors such as an unexpected value passed through an interface.
Deciding which of those a design will treat as errors, and which it will treat
as ordinary results, is a design-time decision.

The rule asks for a commitment of a particular shape: a decision about which
mechanism carries a failure, which conditions count as failures at all, how the
program is left valid afterwards, and where resources are released — taken once,
taken early, and written down where the people writing the code can find it.

## Decision

Decide how errors are detected, transmitted, and recovered from before writing
the code that will need it, and record the decision.

- Answer all four questions up front: how an error is detected, how it reaches
  code that can act on it, how the program is left in a valid state, and how
  resources are released on the failure path.
- Draw the line between an error and an ordinary result deliberately. A
  condition the environment routinely produces is a result to be returned; a
  failure to achieve an advertised purpose is an error.
- Keep the strategy small enough to hold in the head. Prefer one mechanism used
  everywhere to three mechanisms each used where it fits best.
- Where a subsystem cannot follow the strategy — a C library that reports
  through return codes, a dependency that throws — convert at that boundary
  rather than letting a second strategy leak inwards.
- Do not defer the question to "later, once the happy path works". Retrofitting
  a leak-free unwind path onto code written without one means rewriting the
  code, not adding to it.

## Rationale

Error handling is not a feature that can be layered on, because it constrains
every signature in the system. A function that must be able to report failure
has a different return type, a different set of preconditions, and a different
relationship with the resources it holds than one that cannot. Retrofitting
means revisiting all of them.

The sharpest argument upstream makes is about testing rather than design:
untested and rarely executed error-handling code is itself the source of many
bugs. A single early strategy produces failure paths that look alike everywhere,
and paths that look alike are the ones a test suite can actually cover.

## Consequences

### Positive

- Signatures are settled once, rather than churning when the first failure case
  turns up.
- One mechanism used consistently means failure paths that resemble each other,
  which is what makes them testable.
- The error/result boundary is drawn deliberately instead of accumulating from
  whichever call site was written first.

### Negative

- Committing early means committing on incomplete information. The failure
  modes that turn out to matter are often discovered late, and by then the
  strategy is load-bearing across the codebase.
- A single simple strategy fits some subsystems badly, and the misfits are
  permanent. Any mechanism built on return values is a poor fit for
  constructors and operators, which have no return value to carry a failure;
  exceptions are a poor fit wherever a C ABI or a hard real-time deadline
  intervenes. An early decision cannot close those holes, only choose which of
  them to live with.
- The rule is advice about process, not about code, so nothing checks it. A
  project can violate it thoroughly and compile cleanly.
- Boundaries where the strategy is converted — the C libraries, the throwing
  dependencies — are extra code that exists purely to maintain consistency.
- The guidelines give E.1 a single sentence of Reason and no example or
  enforcement. Everything beyond that sentence is drawn from the section's own
  introduction or reasoned out from it, so most of this record cannot be checked
  against upstream argument.

### Neutral

- E.1 does not say which strategy to pick, only that one must be picked early.
  The rules that follow it in this section argue for exceptions and RAII, and
  the ones from E.25 onwards describe what to do where exceptions are
  unavailable.
- A strategy can be revised. Revision is a fresh decision, not an accretion of
  exceptions to the old one.

## Related

- [0290](0290-e-2-throw-an-exception-to-signal-that-a-function-cant-perform-its-assigned-task.md)
  — the strategy the guidelines themselves recommend.
- [0292](0292-e-4-design-your-error-handling-strategy-around-invariants.md) — the
  axis around which the strategy should be organised.
- [0013](0013-p-7-catch-run-time-errors-early.md) — detection, the first of the
  four jobs.
