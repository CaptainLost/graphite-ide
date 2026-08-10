# 0280. CP.60: Use a future to return a value from a concurrent task

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

A future preserves the usual function call return semantics for asynchronous
tasks. There is no explicit locking, and both the correct return, by value, and
the error return, by exception, are handled simply. The upstream rule marks its
example, note and enforcement as unwritten, so the reason above is the whole of
it.

The rule sits in the guidelines' section on message passing, which opens by
observing that the standard-library facilities are quite low-level, focused on
close-to-the-hardware critical programming with threads, mutexes and atomic
types, and that most people should not work at that level, because it is
error-prone and development is slow. Where possible, use a higher-level
facility: a messaging library, a parallel algorithm, or vectorisation.

The alternative this rule displaces is a task that writes its result into a
location the caller also touches. That requires shared writable state, a mutex
or an atomic to guard it, a separate mechanism to say when the result is ready,
and a third convention for reporting that the task failed. The future replaces
all four with the semantics of a return statement.

The two halves of the rule are separable. The value half needs nothing but the
future itself. The error half depends on the task reporting failure by throwing,
so that the exception is stored and rethrown when the result is collected; where
a design does not propagate exceptions, the failure has to be carried in the
returned value instead, and the future still supplies the result and the
completion.

## Decision

Let a concurrent task return its result, and collect it through a future.

- Give a task a return type and a `return` statement, and hand the caller a
  `std::future` for it, rather than passing the task a pointer to write into.
- Collect the result by waiting on the future, so that readiness is signalled by
  the same object that carries the value, with no separate flag or condition
  variable.
- Do not guard the result with a mutex of your own. The library synchronises the
  hand-off between the task and the thread that collects it, so the value is
  safe to read once the future delivers it.
- Let a task's failure travel back through the same future as its result: either
  as an exception, which the future stores and rethrows on collection, or as a
  failure-carrying return type. Do not invent a third channel for it.
- Where a higher-level messaging or parallel facility is available and suits the
  problem, prefer it to hand-built threads and locks.

## Rationale

The gain is that an asynchronous call goes back to being a call. A result, a
completion and a failure are three things every function already communicates,
and the future supplies all three in one object, so nothing has to be invented
per task and nothing can be forgotten.

It also removes the shared writable state that the write-into-a-location
alternative requires, which is the cheapest possible compliance with the rule
against sharing writable data: there is no object two threads both write, so
there is nothing to guard.

## Consequences

### Positive

- Result, completion and failure travel together in one object, so no per-task
  signalling protocol is needed.
- No shared writable state, and therefore no mutex, for handing the result back.
- The task body is ordinary sequential code with an ordinary return.

### Negative

- The standard future is a thin facility: `get()` blocks, it can be called only
  once, and there is no standard way to attach a continuation or compose
  several, so non-trivial asynchronous flow still has to be built by hand.
- Blocking on `get()` in an interactive program's main thread defeats the point
  of running the task concurrently, and nothing in the type prevents it.
- The shared state behind a future is heap-allocated, so this convenience costs
  an allocation per task.
- The simplicity of the error half rests on exceptions, so a design that does
  not propagate them gets only the value half for free and must build the
  failure into the returned type itself.

### Neutral

- A failure-carrying return type inside a future means a caller unwraps twice:
  once for completion, once for success. That is a cost of the arrangement
  rather than a defect in it.
- The upstream rule is a stub with no example and no enforcement clause, so this
  record has no Enforcement section.
- Which facility produces the future — `std::async`, a packaged task, or a pool
  — is not settled here.

## Related

- [0272](0272-cp-41-minimize-thread-creation-and-destruction.md) — the pool a
  task's future can be served from, rather than a thread per task.
- [0259](0259-cp-4-think-in-terms-of-tasks-rather-than-threads.md) — the future
  is what gives a task its result and its completion.
- [0258](0258-cp-3-minimize-explicit-sharing-of-writable-data.md) — returning a
  result is how a task avoids sharing writable state.
- [0269](0269-cp-31-pass-small-amounts-of-data-between-threads-by-value-rather-than-by-reference-or-pointer.md)
  — the same argument applied to the task's inputs.
