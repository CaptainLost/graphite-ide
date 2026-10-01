# 0538. Refresh the working-tree diff when the repository changes, not when it is asked for

Status: proposed
Date: 2026-10-01

## Context

The diff window asks `GitModel::RequestWorkingTreeDiff` on every frame a
working-tree tab is visible. The model's only guard was "one in flight", so the
frame after an answer landed submitted the next full query: a diff plus up to
two 2 MB reads ([0502](0502-read-a-working-tree-file-s-text-from-the-index-and-the-disk.md)),
followed by a delivery that requests another frame. While the tab was on screen
the frame loop never went idle, and the working tree was re-read continuously
for a file nobody had touched.

The single result slot also held whatever landed last, with no record of which
request it answered. A tab that followed its file from the unstaged to the
staged side drew the old side's hunks for a round trip, and a changed request
made while another was in flight was dropped by the in-flight guard.

## Decision

Treat the per-frame call as registering the request the window wants, not as
submitting it. Keep with the slot the request it answers, and submit a query
only when:

- the wanted request differs from the one the last landed query answered, or
- the debounced refresh that re-asks the log
  ([0498](0498-tell-the-application-when-the-repository-s-own-state-changes.md))
  has run since the slot's query was sent.

The refresh only marks the slot stale; the next ask from a visible tab sends
the query. One query is in flight at a time, and a request that changes in the
meantime is sent as soon as the one in flight lands. A failed query counts as
an answer to its request, so a permanent failure is not retried every frame.

`GetWorkingTreeDiff` and `GetWorkingTreeTexts` answer null while the slot holds
the answer to a different request than the wanted one.

## Rationale

A working-tree diff changes only when the working tree or the index does, and
every such change already reaches the model as the dirty mark that refreshes
status, branches and history. Re-asking on that mark is the same rule the log
follows, so one mechanism keeps the whole panel fresh. Keying the answer by its
request is what lets the getter refuse to pass one question's answer off as
another's.

## Consequences

### Positive

- An open working-tree tab costs nothing while nothing changes, and the frame
  loop idles with it on screen.
- A tab that changes its question never draws the previous question's hunks or
  text.
- A changed request made while a query is in flight is no longer lost.

### Negative

- A change the watcher misses and no write of this process causes is now not
  picked up by an open tab at all, where before it was picked up within a
  frame. This is the cost [0498](0498-tell-the-application-when-the-repository-s-own-state-changes.md)
  already names for its allowlist, and it now applies to this diff too.
- Between a tab changing its question and the answer landing, the tab has no
  diff to draw rather than a stale one.
- A transient failure, such as a query that lost a race on `index.lock`, is
  not retried until the next refresh or a change of request.

### Neutral

- The model still holds one slot, so two working-tree tabs would still take
  turns asking into it; the one-tab rule in `ChangesPanel` and
  `DiffWindows::RestoreState` stays.

## Alternatives Considered

- **Re-query on a timer while the tab is open.** Needs no link to the refresh,
  but pays the cost forever on an untouched file and still picks a number
  between stale and busy — the trade 0498 rejected for polling.
- **Submit from the refresh itself, as the log does.** Keeps a tab's request
  fresh without the next frame's ask, but would also re-query for a tab that has
  closed or gone behind another, since nothing tells the model a tab went away.
  The visible tab asks every frame anyway, so letting that ask send the query
  costs at most one frame.

## Related

- [0498](0498-tell-the-application-when-the-repository-s-own-state-changes.md) —
  the refresh this diff now follows.
- [0502](0502-read-a-working-tree-file-s-text-from-the-index-and-the-disk.md) —
  the reads that ride on every working-tree diff query.
