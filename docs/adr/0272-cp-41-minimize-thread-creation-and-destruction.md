# 0272. CP.41: Minimize thread creation and destruction

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Thread creation is expensive.

```cpp
void worker(Message m)
{
    // process
}
void dispatcher(istream& is)
{
    for (Message m; is >> m; )
        run_list.push_back(new thread(worker, m));
}
```

This spawns a thread per message, and `run_list` is presumably managed so as to
destroy those tasks once they have finished. Instead, we could have a set of
pre-created worker threads processing the messages:

```cpp
Sync_queue<Message> work;
void dispatcher(istream& is)
{
    for (Message m; is >> m; )
        work.put(m);
}
void worker()
{
    for (Message m; m = work.get(); ) {
        // process
    }
}
void workers()  // set up worker threads (specifically 4 worker threads)
{
    joining_thread w1 {worker};
    joining_thread w2 {worker};
    joining_thread w3 {worker};
    joining_thread w4 {worker};
}
```

If your system has a good thread pool, use it. If your system has a good message
queue, use it.

## Decision

Create threads once and give them work repeatedly.

- Where work arrives as a stream of items, create a fixed set of worker threads
  at start-up and feed them through a synchronised queue, rather than creating a
  thread per item.
- Use an existing thread pool and an existing message queue where the platform
  or a dependency provides good ones, rather than writing another.
- Size the pool to the machine and the workload, and decide it once rather than
  letting it follow the arrival rate of work.
- Keep the worker threads in a scope that outlives the work, so that their
  creation cost is paid once for the life of the program or subsystem.
- Where a thread really does serve a single long-running purpose, creating it
  directly is fine; this rule is about repetition, not about ever making a
  thread.

## Rationale

The cost being avoided is not only the creation itself but the fact that it is
proportional to the work. A thread per message means the overhead grows exactly
where the system is already busiest, so the design degrades under the load it
was meant to handle.

The pooled version also fixes something the first version leaves open. The bad
example needs `run_list` to be managed so that finished threads are destroyed;
the pooled version has no such bookkeeping, because there is nothing to clean up
per item.

## Consequences

### Positive

- Thread creation cost is paid once at start-up instead of per unit of work.
- The number of concurrently running threads is bounded by design rather than by
  the rate at which work arrives.
- The per-item lifetime bookkeeping the naive version needs disappears entirely.

### Negative

- It introduces a queue, which needs synchronisation of its own and becomes the
  contention point the threads now share.
- A fixed pool size is a guess: too small and work waits behind a slow item, too
  large and the threads compete for the same cores.
- A blocking or long-running item occupies a worker for its duration, so
  unrelated work is delayed in a way that thread-per-item does not suffer from.
- Workers that block waiting for the queue must be given a way to stop, or the
  program cannot shut down; the naive version had no such problem.

### Neutral

- Using an existing pool or queue is preferred to writing one, which makes this
  partly a question of dependencies rather than of code.
- The rule is unenforceable; the upstream rule has no enforcement clause.

## Related

- [0271](0271-cp-40-minimize-context-switching.md) — the more general cost, of
  which thread creation is an expensive instance.
- [0259](0259-cp-4-think-in-terms-of-tasks-rather-than-threads.md) — thinking in
  tasks is what makes a pool possible in the first place.
- [0273](0273-cp-42-dont-wait-without-a-condition.md) — the queue at the centre
  of this design is exactly where that rule applies.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the general rule against
  wasting time.
