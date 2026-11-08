# 0680. Keep each working-tree diff under the request it answers

Status: proposed
Date: 2026-11-08

## Context

[0538](0538-refresh-the-working-tree-diff-when-the-repository-changes-not-when-it-is-asked-for.md)
keyed `GitModel`'s working-tree diff by the request it answers, but kept one
slot for it. Every place that shows a working-tree diff then had to keep the
others out of that slot, each with its own warning: the Changes side panel
closed the working-tree tab before opening another, the Git window's Local
changes preview closed any working-tree tab when a file was picked in it and
gave up its own diff when a tab of another file had been opened since,
`DiffWindows::RestoreState` restored only the first working-tree tab, and the
diff body warned that the diff was not cached by key. The Git window's preview
was the third such caller.

## Decision

- `GitModel` keeps working-tree answers in an `LruResultCache` keyed by the
  `DiffRequest`, eight at most, each answer holding the diff and the texts read
  in the same job. Every getter takes the request whose answer it reads.
- The rule of 0538 holds per request: an ask in flight, answered or failed asks
  nothing; the refresh marks every held answer stale, so its next ask sends the
  query again while the last good answer stays on screen; a failure counts as an
  answer and leaves the last good one shown.
- `LruResultCache` gains `MarkAllStale`, and a success clears a failure the entry
  held.
- No caller coordinates around the diff any more. The Git window's preview no
  longer closes a tab or gives way to one, and a restore opens every
  working-tree tab it held.
- The Changes side panel still replaces its last working-tree tab with the next
  file picked, now as what that list does rather than as a slot constraint.

## Rationale

What answers a request is the model's concern; with one answer per request the
model holds what each view asks for, and the views that ask stop knowing about
each other. A commit's diff is already cached this way.

## Consequences

### Positive

- A working-tree diff tab and the Git window's preview of another file show
  side by side.
- Two working-tree tabs restored from a folder's state both come back.

### Negative

- After a refresh every working-tree diff on screen is asked again, each in its
  own job, where one slot asked one; the cap of eight bounds what is held, not
  what is on screen.
- Queries for different requests run at once rather than one after another.

### Neutral

- The Changes side panel's one-tab rule is kept as behaviour and can now be
  dropped without touching the model.

## Alternatives Considered

- **Keep the slot and let the newest ask win.** It is what the callers' warnings
  worked around: two views on screen take turns, and each turn re-reads the
  working tree.
- **Drop the side panel's one-tab rule too.** Deferred: it changes what a click
  in the list does, which is a choice about the list, not about the model.

## Related

- [0538](0538-refresh-the-working-tree-diff-when-the-repository-changes-not-when-it-is-asked-for.md) —
  amended: the refresh rule stays, the single slot goes.
