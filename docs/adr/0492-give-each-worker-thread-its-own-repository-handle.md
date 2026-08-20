# 0492. Give each worker thread its own repository handle

Status: proposed
Date: 2026-08-20

## Context

`GitModel` runs every git query on `Core::JobQueue`'s worker threads rather
than the UI thread, so a status refresh does not stall a frame. libgit2's
`git_repository` is not safe to use from two threads at once, so whatever
opens it has to answer two questions: how many handles exist, and which
thread each one belongs to.

The queue can run a status query and a blame query at the same time, on
different workers — that pairing is exactly the one this design cares about,
since blame runs on a large file and can take long enough that a status
refresh landing behind it would make the status bar visibly lag.

## Decision

`RepositoryGitPort` opens one `Core::Repository` per worker thread, cached in
a `thread_local` map keyed by the repository root, and opened lazily the
first time that thread needs it. No handle is ever touched from a thread
other than the one that opened it. A handle is not closed when the workspace
changes; it stays in its thread's cache and a later query against the same
root reopens nothing.

## Rationale

Per-thread handles let independent queries run independently, which is the
property the concurrency design needs: a blame in flight must not hold up a
status refresh. Lazy, thread-local opening means the cost is paid only by
threads that actually query git, and only once per root per thread, rather
than upfront for every worker `JobQueue` happens to have started.

## Consequences

### Positive

- A blame of a large file and a status refresh run concurrently instead of
  queueing behind one shared handle.
- No handle is ever accessed from two threads, so there is no mutex to hold
  around a `git_repository` call and no risk of the corruption that sharing
  one would cause.
- Opening is lazy: a worker that never runs a git job never opens a
  repository at all.

### Negative

- A handle lives until its thread ends, not until the workspace closes, so a
  worker that queried an old root before the workspace changed keeps that
  root's handle open in its cache for the rest of its life. This is
  deliberate — `JobQueue` runs a handful of long-lived workers and this
  application has one open root at a time, so the wasted handle is one per
  worker, not one per folder ever opened — but it means the cache does not
  shrink on its own.
- `GitModel` requires, as a consequence of how it submits work, that the
  `Core::JobQueue` behind its submitter be destroyed — and its workers
  joined — before `GitModel` itself is, because every job it submits
  captures `this`. An owner holding both must declare `GitModel` before the
  queue in member order, so that reverse destruction order tears the queue
  down first. Declaring them the other way compiles without complaint and
  fails only under real threading, when a worker is mid-query against a
  model that has already been destroyed — the kind of bug that does not show
  up under the inline, same-thread submitter the tests use.

### Neutral

- The map is keyed by root as a string rather than by nothing, even though
  there is only one root open at a time: a worker still holding a stale
  handle for the previous root when the workspace changes falls through to
  opening a second entry rather than reusing a handle that points at the
  wrong repository.

## Alternatives Considered

### One dedicated git thread, no per-thread handles

The simplest way to satisfy "not from two threads at once": route every git
query through a single thread and one repository handle. Rejected because it
serialises exactly the pair of queries that need not wait on each other — a
blame of a large file would block a status refresh behind it, and the status
bar is the thing this design is most protective of staying responsive.

### One handle guarded by a mutex, shared across workers

Keeps a single `Core::Repository` and serialises access with a lock instead
of giving every thread its own. Rejected for the same reason as the dedicated
thread: it turns concurrent queries into queued ones, just with a mutex
standing in for a queue, and buys nothing over the dedicated-thread option
that the per-thread cache does not already buy more cheaply.

## Related

- [0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) — where
  this port sits in the Core/Graphite split.
- `docs/superpowers/specs/2026-08-20-git-ui-design.md` — the design this
  record was extracted from.
