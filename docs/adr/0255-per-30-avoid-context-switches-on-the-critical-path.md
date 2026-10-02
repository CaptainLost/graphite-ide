# 0255. Per.30: Avoid context switches on the critical path

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Upstream closes the performance section with a title and no body — no reason, no
example, no enforcement. There is nothing to transcribe, so this record states
the rule as given.

## Decision

Avoid context switches on the critical path.

- Where a path has been identified as critical, keep off it the operations that
  can hand the processor to another thread: blocking calls, waits, sleeps, and
  contended locks.

## Rationale

A context switch takes the processor away from the thread until the scheduler
returns it, so the delay it introduces is not one the program decides — which is
what makes it a concern on a path that has a deadline.

## Consequences

### Positive

- A path that cannot be descheduled has a latency that depends on the program
  rather than on the scheduler.

### Negative

- The record rests on a title alone. Upstream supplies no reason and no
  definition of "critical path", so neither the cost being avoided nor the scope
  of the rule can be checked against the guidelines.
- The cure is concurrency — background threads, result queues, cancellation, and
  the lifetime and data-race questions that come with them — which the rule does
  not weigh against the cost it removes.
- It is unenforceable mechanically. No compiler diagnoses "this call may block",
  and blocking constructs are ordinary-looking function calls.

### Neutral

- The rule concerns switches on the path, not the use of threads in the program.
- It says nothing about how work kept off the path should instead be done.

## Related

- [0250](0250-per-15-do-not-allocate-on-a-critical-branch.md) — the other bare
  rule about the same path.
- [0240](0240-per-3-dont-optimize-something-thats-not-performance-critical.md) —
  the rule with upstream reasoning about what makes code critical at all.
- [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md) —
  scheduling delays hide in the tail and are invisible to an average.
