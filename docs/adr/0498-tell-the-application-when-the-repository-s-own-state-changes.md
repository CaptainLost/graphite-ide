# 0498. Tell the application when the repository's own state changes

Status: proposed
Date: 2026-08-21

## Context

The file watcher drops every change under `.git`, with one exception for
`info/exclude`. The reason is sound: git rewrites objects, lock files, edit
messages and reflogs on every operation, and refreshing the file tree for each
one would be a storm of no-op work.

The cost of that rule only showed once the git panel existed. A commit,
checkout, fetch, rebase or reset run in a terminal beside the application
changes nothing in the working tree — it moves a ref and rewrites the index.
Those are all inside `.git`, so no event was published at all, and every git
answer on screen went quietly stale: the history kept showing what it showed
when the panel was opened, and files just committed were still listed as
modified.

Compounding it, `GitModel` refreshed only the status and the branches when an
event did arrive. Nothing re-asked for the log, so even editing a file in the
tree — which does publish an event — never brought a new commit into the list.

The two are independent. Either one alone is enough to show a stale history.

## Decision

Name the few paths under `.git` that say what a repository *is* rather than
what it holds: `HEAD`, `index`, `packed-refs`, and everything under `refs`.
`Core::NamesRepositoryState` decides, and the watcher reports a change to one
of them as `Changes::RepositoryStateChanged` — a flag, not a directory.

Carry it as its own event, `RepositoryChangedEvent`, published by `Workspace`
beside `WorkspaceFilesChangedEvent` rather than folded into it. The event is
empty: what moved is a question every subscriber answers by asking git again.

Have `GitModel` remember the last `LogRequest` and repeat it on every refresh,
so a refresh answers the question the reader is actually looking at — their
ref, their filter, their page size — and not a default one.

## Rationale

The allowlist is small, and every path on it changes exactly when the answer on
screen changes. That is the property that makes it safe to let through what the
blanket rule exists to keep out: `refs` moves once per commit or fetch, not once
per written object.

Keeping the signal separate from the file-change event is what stops a branch
moving from costing a file-tree refresh. The two events describe different
things, and a subscriber to one has no use for the other.

Remembering the request rather than re-deriving it keeps the knowledge in one
place. The alternative — having the view notice a refresh and re-ask — puts the
same duty on every view that ever shows a log, and a view that forgets it is a
view that silently drops the reader's filter.

## Consequences

### Positive

- A commit, checkout, fetch or rebase made outside the application is visible in
  it, without polling and without watching all of `.git`.
- The status stops lying after a commit made elsewhere: files just committed no
  longer show as modified.
- One refresh keeps the whole panel honest — status, branches and history
  together — because they all hang off the same debounced mark.

### Negative

- The allowlist is a list, and a list can be missing something. A future feature
  that reads, say, `MERGE_HEAD` or a stash ref will need it extended, and
  nothing will fail loudly when it is not — the answer will simply be stale.
  This is the real cost, and it is the price of not watching all of `.git`.
- Refreshing now re-walks the history. Under a filter that is a walk bounded by
  `MaxWalk` rather than a cheap read, so a repository whose refs move constantly
  costs more than it did.
- `Changes` grew a field, so every caller of `TakeSettled` has one more thing it
  may ignore.

### Neutral

- `index` is on the list for the status rather than the history, so a `git add`
  in a terminal refreshes the panel too.

## Alternatives Considered

- **Watch all of `.git`.** One line to delete, and nothing to keep in step.
  Rejected because it re-introduces exactly the storm the filter was written to
  stop: git writes hundreds of objects during a fetch, each one an event.
- **Poll the repository on a timer.** No watcher changes at all, and it cannot
  miss a path the way an allowlist can. Rejected because the cost is paid
  forever, on every repository, whether or not anything changed — and the
  interval is a choice between a stale panel and a busy one.
- **Fold the flag into `WorkspaceFilesChangedEvent`.** One event, one
  subscription. Rejected because every existing subscriber refreshes files on
  it, so a branch moving would refresh the file tree for nothing.
- **Have the git view re-ask for the log when it notices a refresh.** No new
  state in the model. Rejected because it makes every view that shows a log
  responsible for remembering the reader's filter, and the model is the one
  thing that already knows what was asked.

## Related

- [0496](0496-filter-the-history-in-the-walk-not-in-the-page.md) — the filter
  that a repeated request has to carry.
