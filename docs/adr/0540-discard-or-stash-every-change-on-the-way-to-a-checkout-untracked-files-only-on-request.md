# 0540. Discard or stash every change on the way to a checkout, untracked files only on request

Status: proposed
Date: 2026-10-01

## Context

When a checkout would overwrite work ([0539](0539-plan-every-checkout-and-ask-before-it-would-overwrite-work.md)),
the user has to choose what happens to that work. A choice per file would make a
long dialog for what is almost always one intent: get this work out of the way,
or keep it for later.

Three kinds of work are in play. Tracked changes, staged or not, are in git's
view. Unsaved editor text is in the editor's, and a stash takes what is on disk.
Untracked files are the third kind, and the riskiest to touch: a build output
directory or a scratch file is untracked too, and may be large or irreplaceable.
An untracked file can also stand in the way itself, when the target tracks a
file of the same name.

## Decision

The dialog lists every change, not only the blocking ones, because both actions
act on every change. Files with unsaved editor text are marked. It offers:

- **Discard** throws away every change in the working tree: staged, unstaged and
  the unsaved text of open documents, which are reverted to disk. It is drawn in
  the warning colour and says it cannot be undone. Then it checks out.
- **Stash** first saves the unsaved documents, because a stash takes what is on
  disk, then stashes every change, then checks out. A stash that finds nothing to
  save counts as done.
- **Cancel** changes nothing.

Untracked files are included in Discard and Stash only when the user ticks
"Include untracked files in Discard and Stash". The box starts unticked. When an
untracked file would itself be overwritten, it is marked "(blocks checkout)" and
the box is ticked and locked, because the checkout cannot go ahead with that
file in place.

Ignored files are never touched.

## Rationale

Both actions are all-or-nothing so that, after either, the tree matches the
target and the user knows exactly what is where. A choice per file would invite
a half-moved tree that is hard to reason about. Untracked files are opt-in
because they are the only kind git holds no version of, so including them by
default would make Discard destroy things nothing can restore, and Stash sweep
up build output.

## Consequences

### Positive

- One decision covers all the work, and the dialog says what each button takes.
- A stash made at checkout can be applied again from the Git panel
  ([0541](0541-put-branch-and-stash-actions-in-the-git-panel-s-right-click-menus.md)).
- The default never touches untracked files, and when it must, it says so.

### Negative

- Discard also reverts unsaved editor text, which nothing can bring back. The
  dialog marks those files, but there is no undo.
- A user who wants to keep some changes and discard others has to leave the
  dialog, deal with them in the Changes panel, and check out again.
- A locked checkbox means a blocking untracked file forces untracked files into
  Discard and Stash whether or not the user wanted them.

### Neutral

- If the checkout is refused after a successful stash, the work stays stashed
  and the tree is clean.

## Alternatives Considered

- **A choice per file.** The most flexible, but a long dialog for a decision that
  is nearly always uniform, and it leaves the tree in a state the user has to
  reason about file by file.
- **Include untracked files by default.** One fewer checkbox, but Discard would
  silently delete files no history holds.
- **Stash only the files in the way.** Keeps more work in place, but the rest of
  the changes then travel to a branch that may not suit them, and the user is
  not told which.

## Related

- [0539](0539-plan-every-checkout-and-ask-before-it-would-overwrite-work.md) —
  the plan that decides when this dialog is shown.
- [0475](0475-ask-every-modal-question-through-modalquestion.md) — the modal it
  is asked through.
