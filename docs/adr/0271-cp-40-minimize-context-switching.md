# 0271. CP.40: Minimize context switching

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Context switches are expensive. That is the whole of the upstream rule: it has
no example and no enforcement clause, both being marked as unwritten in the
source.

The cost is invisible in the program text. Suspending a thread and resuming
another means entering the scheduler, saving and restoring register state, and
usually losing the cache and translation-lookaside-buffer contents the thread
had warmed. The work is real but it is attributed to nothing in particular, so a
design that switches often looks fine in the code and poor in a profile.

The other rules in this section are largely the concrete form of this one.
Waiting on a mutex, waiting on a condition variable, creating a thread and
destroying a thread all cause switches, and each has a rule of its own about
avoiding the ones that were not necessary.

## Decision

Design so that threads run for a while when they do run.

- Prefer a small number of long-lived threads that pull work from a queue to
  many short-lived threads that each do one thing.
- Keep critical sections short so that threads block on them rarely, and do not
  block at all inside a lock.
- Batch work rather than handing off a single item at a time, since each
  hand-off between threads is a potential switch.
- Do not create more runnable threads than the machine has hardware threads for
  compute-bound work; oversubscription buys switches, not throughput.
- Treat "minimize context switching" as a claim to be measured rather than
  assumed, since the cost is invisible at the call site.

## Rationale

The reason this deserves a rule of its own, despite having no code in it, is
that nothing in the source attributes the cost. A hand-off between threads is
one function call to write and can be hundreds of times the cost of the work
handed off, and no reading of the code will show that.

It is also the rule that connects the others. Shorter critical sections, pooled
threads and waits with conditions are all specific ways of not paying this cost,
which is why it is worth stating once in general.

## Consequences

### Positive

- Names the cost that the more specific performance rules in this section are
  all avoiding.
- Discourages designs that hand single items between threads, which are the
  usual source of unexpected switch counts.
- Encourages measuring rather than assuming, since the cost is invisible in the
  source.

### Negative

- It gives no threshold and no technique, so on its own it cannot be applied;
  the work is in the specific rules.
- Taken too far it argues against concurrency altogether, and against
  responsiveness — a design that never yields is not obviously better than one
  that switches.
- Batching to reduce switches increases latency for the first item in a batch,
  which is a direct trade against responsiveness in an interactive program.
- It is unenforceable; the upstream rule has no enforcement clause, and switch
  counts are a property of the run, not of the code.

### Neutral

- The upstream rule is a stub: reason only, with the example and enforcement
  marked as unwritten.
- Whether a given switch matters depends entirely on the ratio between it and
  the work being done, which is a measurement, not a rule.

## Related

- [0272](0272-cp-41-minimize-thread-creation-and-destruction.md) — creating a
  thread is one of the more expensive ways to cause switches.
- [0274](0274-cp-43-minimize-time-spent-in-a-critical-section.md) — thread
  suspension and resumption is the cost that rule cites too.
- [0273](0273-cp-42-dont-wait-without-a-condition.md) — waking up to find there
  is no work is a switch spent on nothing.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the general rule, of which
  this is a concurrency-specific instance.
