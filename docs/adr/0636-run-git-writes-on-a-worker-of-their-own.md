# 0636. Run git writes on a worker of their own

Status: proposed
Date: 2026-10-25

## Context

[0500](0500-serialise-git-writes-in-one-queue.md) serialises writes in `GitModel`, one in
flight at a time. The write job itself was submitted to the same multi-worker
`Core::JobQueue` as every read. The queue is first in, first out, so a checkout could wait
behind any long read before starting:

- "Load all" on a large history;
- the walk that collects branch authors;
- a commit count.

Meanwhile its controls were already greyed out.

## Decision

Submit git writes to a `Core::JobQueue` with one worker, owned by `GraphiteLayer` beside
the shared queue. `GitModel` keeps its serialisation and outcomes exactly as 0500 describes;
only where the job runs changes. The write queue is declared so that it is destroyed, and
its worker joined, before the models and the port it captures.

Amends [0500](0500-serialise-git-writes-in-one-queue.md).

## Rationale

A write is the direct answer to a click. Reads are refreshes that can wait. One more thread
is the smallest change that stops a click from queueing behind a refresh.

## Consequences

### Positive

- A write starts as soon as the previous write ends, whatever the reads are doing.

### Negative

- One more thread, idle almost always.
- Destruction order in `GraphiteLayer` has one more member to keep right.

### Neutral

- A read and a write could already pass each other on different workers (0500's
  Consequences). That is unchanged.

## Alternatives Considered

- **Priorities in `Core::JobQueue`.** A write would still wait for a busy worker to finish
  its current long read.
- **More workers in the shared queue.** That only makes the wait less likely, and adds
  threads that compete with libgit2 reads for the disk.

## Related

- [0500](0500-serialise-git-writes-in-one-queue.md),
  [0635](0635-report-each-git-write-in-the-status-bar-from-the-click-until-the-status-after-it-lands.md)
