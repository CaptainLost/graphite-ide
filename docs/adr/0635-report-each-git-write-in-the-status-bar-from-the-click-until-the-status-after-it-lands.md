# 0635. Report each git write in the status bar from the click until the status after it lands

Status: proposed
Date: 2026-10-25

## Context

Git writes queue in `GitModel`
([0500](0500-serialise-git-writes-in-one-queue.md)) and report failures in toasts
([0557](0557-report-a-failed-action-in-a-toast-rather-than-in-the-panel-that-issued-it.md)).
While one runs, the only sign is that the buttons that start writes are greyed out. A
checkout of a large branch shows nothing for its plan or its write, and the status bar keeps
naming the old branch. When a write ends, the buttons come back before the status read after
it has landed. For a moment, rows that were just staged or discarded still look unstaged,
with live buttons. Only a commit says it succeeded.

## Decision

- **Show the write in the status bar** from the click to the landing of the status read
  that follows it, as a spinner and a sentence:
  - "Checking out feature/x…"
  - "Staging 2,000 files…"
  - "Discarding changes in 3 files…"
  - "Applying stash@{0}…"
  - "Creating branch x…"
  - and so on for every other write.
  
  A checkout covers both its plan and its write.
- **Then confirm it briefly**, for example "Checked out feature/x" or "Staged 2,000 files".
  A commit's existing "Committed <hash>" becomes one case of this.
- **Keep write controls disabled until that status read lands**, not only until the write
  returns, as the init prompt already did.
- **Mark the checkout target on its branch row** with the loader while the checkout runs.
- **Keep an answer that needs a question, such as a checkout plan or a delete check, until
  the Git panel is drawn**, rather than asking and withdrawing it while another tab is in
  front. Until then, the status bar says the action is waiting for an answer in Git.

## Rationale

The status bar is visible from every tab, and a write's result lands in more than one
panel. The status read after the write is what the reader actually waits for: before it,
the screen still shows the old state.

## Consequences

### Positive

- Long checkouts and large stage or discard operations visibly run and visibly end.
- The double-discard window after a discard is gone.

### Negative

- Controls stay disabled a little longer after each write, for the debounce plus one status
  read.
- `GitModel` must know each write's description and when "the status after it" has landed.

### Neutral

- Failures stay toasts.

## Alternatives Considered

- **A loader in the panel that issued the write.** A checkout started from the history or a
  dialog has no single panel, and the reader may switch tabs while it runs.
- **Optimistic rows**, moving a staged row before the write lands. That needs undo when the
  write fails, which is a model of its own for a few hundred milliseconds of gain.

## Related

- [0500](0500-serialise-git-writes-in-one-queue.md),
  [0557](0557-report-a-failed-action-in-a-toast-rather-than-in-the-panel-that-issued-it.md),
  [0636](0636-run-git-writes-on-a-worker-of-their-own.md),
  [0633](0633-show-every-wait-with-the-core-loader-and-say-nothing-is-there-only-after-an-answer-said-so.md)

Superseded in part by [0657](0657-open-the-git-window-on-a-navigator-of-work-changes-branches-and-stashes-beside-one-main-area.md):
the branch row that carried the checkout's loader is gone with the old refs column; the
Git window's navigator header names the checkout with the loader instead, as the status
bar does.
