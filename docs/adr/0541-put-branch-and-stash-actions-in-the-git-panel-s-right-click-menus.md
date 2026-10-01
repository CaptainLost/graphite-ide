# 0541. Put branch and stash actions in the Git panel's right-click menus

Status: proposed
Date: 2026-10-01

## Context

The status bar shows the current branch and repository state, read-only
([0535](0535-show-git-state-in-the-status-bar-read-only-and-keep-git-actions-in-the-panels.md)).
Branches and stashes can now be checked out, created, renamed, deleted, applied,
popped and dropped, and the actions have to be reachable from somewhere that
shows what they act on. The Git panel's ref column already lists local and remote
branches and now lists stashes, and its history lists commits.

Several of the actions destroy something. A stash is addressed by position in the
list, and that position names a different stash as soon as another is pushed or
dropped.

## Decision

Every branch and stash action lives in the Git panel and is reached from a
right-click menu on the row it acts on. Each menu opens through
`Core::BeginRightClickPopup`. Unavailable items are greyed, not hidden. Double
click does nothing, and the status bar stays read-only.

| On | Items |
|---|---|
| Local branch | Checkout, New Branch from Here, Rename, Delete |
| Remote branch | Checkout, New Branch from Here |
| Commit in the history | Checkout (detached), New Branch from Here |
| Stash | Apply, Pop, Drop |

Checking out a remote branch creates a local branch of the same short name that
tracks it and switches to it, or switches to the local one when it exists.
Deleting a remote branch is a network operation and is not offered. The Changes
panel gains Stash Changes, which opens a dialog with an optional message and
"Include untracked", unticked.

Delete and Drop always ask. Deleting a branch whose commits are reachable from no
other branch says how many would be lost and needs "Delete anyway". Branch names
are validated as typed: an invalid or existing name greys OK and says why.

Stashes are addressed by id, not index: `StashApply`, `StashPop` and `StashDrop`
take the stash's object id, and an id no longer in the list is `NotFound`.

All of these run as writes on the model's existing queue, with the same ordering,
in-flight flag, error line and refresh as the Changes panel's writes. The Git
panel shows the write error the same way the Changes panel does.

## Rationale

A row is the thing the action applies to, and a right-click menu is the place a
user already looks for an action on a row, without a permanent button per row.
Greyed items show what exists and why it is unavailable. Addressing stashes by
id means a menu opened on one stash cannot act on another when the list changes
between the click and the write.

## Consequences

### Positive

- Every action is next to the row it acts on, and unavailable ones are shown
  rather than missing.
- A stash action cannot hit the wrong stash after the list changes.
- Branch and stash writes share the queue, error line and refresh with the
  existing writes.

### Negative

- A right-click menu is not discoverable on its own.
- The status bar shows the branch but cannot change it.
- A stash dropped elsewhere between the click and the write is reported as not
  found rather than acted on.

### Neutral

- Remote operations (fetch, pull, push, deleting a remote branch) and merge,
  rebase and tags are not offered.

## Alternatives Considered

- **Make the status bar's branch indicator a branch switcher.** Convenient, but
  breaks the rule that the status bar is read-only and puts a destructive action
  where a click is cheap.
- **Per-row buttons.** Discoverable, but a button on every branch, commit and
  stash row crowds a narrow column.
- **Address stashes by index.** Simpler to type, but the index changes under the
  user whenever another stash is pushed or dropped.

## Related

- [0535](0535-show-git-state-in-the-status-bar-read-only-and-keep-git-actions-in-the-panels.md)
  — the status bar stays read-only.
- [0539](0539-plan-every-checkout-and-ask-before-it-would-overwrite-work.md) and
  [0540](0540-discard-or-stash-every-change-on-the-way-to-a-checkout-untracked-files-only-on-request.md)
  — what a checkout does once started.
- [0475](0475-ask-every-modal-question-through-modalquestion.md) — the modal the
  dialogs use.
