# 0500. Serialise git writes in one queue

Status: proposed
Date: 2026-08-21

## Context

[ADR 0499](0499-let-core-repository-write-not-only-read.md) gave
`Core::Repository` five write methods. They are synchronous and they touch
`index.lock`, so calling them from the UI thread would stall a frame and
calling two at once would fail on the lock. Something in `Graphite` has to
decide when they run and in what order.

`GitModel` already owns the machinery a write needs: it runs work off the UI
thread through `Core::JobQueue`, it carries a generation counter so a result
for a closed folder is dropped, and it has a `MarkDirty` path that
[ADR 0498](0498-tell-the-application-when-the-repository-s-own-state-changes.md)
uses when the repository changes underneath the application.

The panel driving these writes is a list of buttons. A user clicking *stage* on
three rows in quick succession produces three requests before the first has
landed.

## Decision

**Writes live in `GitModel`, in a second translation unit,
`GitModelWrites.cpp`.** Not in a second model beside it.

**Writes queue FIFO with one in flight.** Excess requests are queued, never
rejected. Serialisation is enforced in the model rather than in the port,
because the model is the only place that sees every write at once.

**A write never participates in the generation counter as an ordering rule.**
There is no such thing as a stale commit, so a write result is never discarded
for being out of date. The generation is carried for exactly one purpose: a
completion whose generation no longer matches is dropped whole, so a write in
flight when the folder closes neither marks a new repository dirty nor reports
an error about the old one.

**`Clear()` empties the queue and deliberately leaves the in-flight flag
alone.** A running write is still running; clearing the flag would let a second
write start beside it, against the same `index.lock`. A write enqueued after
`Clear()` is not lost and does not start early — it waits for the stale one to
land.

**A write must always produce an outcome.** `RunWrite` catches every exception
and converts it to a failure. This is not defensive habit; see the Rationale.

**Success marks the model dirty through the same `MarkDirty` the watcher
uses**, so status, branches and history all refresh through one mechanism.
There is no separate "after a commit, refresh the log" rule. The model marks
itself rather than waiting for the watcher, because the tests run with no
watcher at all and because ADR 0498 says plainly that its allowlist can miss a
path.

A write's failure is kept in `GetWriteError()`, apart from `GetLastError()`: a
write is the direct answer to a click, so it belongs to the panel that issued
it, not to the list of refresh failures. `TakeCommitted()` hands back a
successful commit's `Oid` once and forgets it.

Amended by [0557](0557-report-a-failed-action-in-a-toast-rather-than-in-the-panel-that-issued-it.md):
failures are now queued and handed out once through `TakeWriteError()`, and
reported in a toast rather than in the panel.

Amended by [0636](0636-run-git-writes-on-a-worker-of-their-own.md): the write
jobs run on a one-worker queue of their own instead of the shared read queue.

## Rationale

**Why one model.** The argument for a second model — that `GitModel` is already
large, and that a write does not participate in its generation discipline — is
a real one, and it is the reason this was considered at all. It does not
survive contact with what a write is *for*. A write's whole purpose is to make
the read model stale, and the read model is `GitModel`. Splitting them produces
two objects tied by a callback whose only job is to reunite them: the coupling
is not removed, only hidden behind an indirection that has to be traced to be
understood.

**Why the read pattern could not simply be copied.** This is the sharpest
illustration of the asymmetry between a read and a write, and it was found by
being bitten. `Core::JobQueue` catches an exception escaping a job and abandons
that job — both `catch (const std::exception&)` and `catch (...)` — because an
exception leaving a thread function would call `std::terminate` and take the
process down. For a read that is harmless: the generation is never applied, and
the next debounce re-asks. For a write it was fatal, and fatal invisibly. The
job never reached its completion, so the in-flight flag was never cleared;
every later write queued forever behind a write that had already stopped
existing; the panel's buttons stayed greyed out; no error was ever reported;
and because `Clear()` deliberately does not reset that flag, closing and
reopening the folder did not recover it either. Only restarting the application
did. A read may be abandoned because something will ask again. Nothing asks a
write again, so a write must end in an outcome no matter what happens inside
it.

**Why queueing beats rejecting.** Three clicks are three intentions. Rejecting
the second and third turns a busy moment into silently dropped input, and a
button that sometimes does nothing is worse than one that is briefly slow.

**Why one write at a time.** Two writes against one index race on
`index.lock`, and the loser fails for a reason that has nothing to do with what
the user asked. Serialising costs latency the user cannot perceive at this
granularity.

## Consequences

### Positive

- Every write reaches the user as either a success or a named failure. There is
  no path that leaves the panel waiting for something that will never arrive.
- Requests leave in the order they arrived, and never two at once, which is
  provable in `GraphiteTests` against a fake port and an inline submitter — no
  threads, no libgit2.
- One refresh mechanism covers a write made in the panel and a change made in a
  terminal beside it, so there is one thing to keep correct instead of two.
- The write error and the refresh error stay separate, so a failed stage does
  not vanish under a later refresh failure, nor vice versa.

### Negative

- A read on one worker and a write on another can pass each other: the reader
  holds its own handle
  ([ADR 0492](0492-give-each-worker-thread-its-own-repository-handle.md)) and
  will see the index either before or after the write, depending on which
  touched `index.lock` first. This is not corruption and is not prevented here,
  because the write marks the model dirty on completion and a result from
  before the write is replaced immediately. It can still show a stale status
  for one refresh, and someone who sees a single flicker should know it is a
  known cost rather than a fault.
- The in-flight flag surviving `Clear()` means a folder closed while a write is
  running leaves the model unable to start the next write until the old one
  lands. That is correct, but it means "the folder is closed" is not a full
  reset of the write path.
- `GitModel` grows a second responsibility and a second translation unit. A
  reader asking "what does the model do" now has two files to read, and the two
  halves share state that neither one owns alone.
- A working-tree diff first shipped without the whole-file syntax colouring a
  commit's diff gets, because `GitPort::ReadFileTexts` reads both sides of a
  file out of commits — that is what its `Oid` parameter is — and a commit is
  the one thing neither side of a working-tree diff is. The hunks drew; the text
  behind them did not. That gap was closed in
  [0502](0502-read-a-working-tree-file-s-text-from-the-index-and-the-disk.md),
  and the note is kept here rather than deleted because the shape of the
  omission — a read model built entirely around commits — is the same shape this
  record is about.

### Neutral

- The commit message, the amend toggle and the row selection stay in the panel.
  They are view state, and this record does not move them into the model.

## Alternatives Considered

### A second model beside `GitModel` for writes

Attractive because `GitModel` is already large, and because a write genuinely
does not obey the generation discipline the reads are built around — the two
really are different in kind. Rejected because they are not independent: a
write exists to invalidate the read model, so the second object would need a
callback into the first for the only thing it does that matters. That is the
same coupling with an extra hop.

### Reject a write while one is in flight

Simplest possible rule, and it needs no queue. Rejected because it makes the
panel's behaviour depend on how fast the user clicks: staging three rows would
stage one and discard two clicks, with nothing on screen to say why.

### Let the port serialise, with a mutex around the repository handle

Puts the rule next to the thing it protects. Rejected because the port is
per-thread by design (ADR 0492) and sees one call at a time; only the model
sees the whole set of pending writes, and only the model can decide what
happens to the ones that have not started.

### Rely on the file watcher to notice the write

No explicit `MarkDirty` at all — the write changes `.git`, the watcher fires,
the model refreshes. Rejected on two counts: the tests run with no watcher, so
nothing about the refresh would be provable headless; and ADR 0498 records that
its allowlist is a list and a list can be missing something. If the watcher
catches the same write a second time the cost is one duplicated refresh inside
the debounce window, which is cheaper than a refresh that never happens.

## Related

- [0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) — the
  Core/Graphite split this queue sits on the Graphite side of.
- [0492](0492-give-each-worker-thread-its-own-repository-handle.md) — why a
  read and a write can be in flight on two threads at once.
- [0498](0498-tell-the-application-when-the-repository-s-own-state-changes.md) —
  the `MarkDirty` path a successful write reuses.
- [0499](0499-let-core-repository-write-not-only-read.md) — the Core methods
  this queue calls.
- `docs/superpowers/specs/2026-08-21-git-writes-design.md` — the design this
  record was extracted from.
