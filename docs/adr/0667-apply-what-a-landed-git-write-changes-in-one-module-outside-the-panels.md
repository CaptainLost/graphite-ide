# 0667. Apply what a landed git write changes in one module outside the panels

Status: proposed
Date: 2026-11-03

## Context

`GitModel` hands out what a landed write changed through one-shot queues: the
committed id, the paths a discard or a checkout rewrote, the failures, and a
count of writes that may have touched files. Each queue answers once, so each
had exactly one drainer, fixed only by comments. The side panel's Changes host
took commits and discards (0656 chose it, and listed as a cost that draining
depended on that panel being registered), the Git window took checkouts, and the
layer took failures and the write count. The two editor announcements used
different policies through different seams, and a discarded folder's files
travelled from the confirm to the landing through a map in `ChangesShared`,
cleared only by draw code.

## Decision

- `Git/WriteConsequences` takes every queue once per frame, right after
  `GitModel::OnUpdate`, whatever is on screen. It tells the editor what a
  discard or a checkout rewrote, through `EditorBuffers`; clears the shared
  commit draft after a commit; hands a failure to the layer's toast; and tells
  the change markers and blame once per write that may have touched files.
- The policies stay as they were: a discard announces every file it rewrote,
  since its question named the unsaved edits it throws away; a checkout
  announces only documents without unsaved edits, since its question offered to
  keep them.
- A discard request carries the files it rewrites, as listed when it was asked,
  and the model reports those once it succeeds. The map in `ChangesShared` is
  gone.
- No panel drains anything. The Changes side panel only withdraws its questions
  when it is not drawn.

## Rationale

What follows a write is one concern with one set of rules, and it now has one
owner that tests drive with a fake port and a fake editor, independent of which
host made the write or whether any host is drawn.

## Consequences

### Positive

- A commit made from the Git window clears the draft with the side panel
  closed or unregistered.
- A discarded folder announces its own files from its own request, so no listing
  can be left behind for a later discard to pick up.

### Negative

- The queues themselves remain one-shot, and the module must stay their only
  reader; a second reader would starve it.

### Neutral

- Branch renames are still followed from branch snapshots by the Git window and
  the reviews; they are not part of this module yet.

## Alternatives Considered

- **One landed-write record from the model in place of the queues.** Deferred:
  it would rewrite most of the model's write tests for the same behaviour, and
  the consequences can move first.
- **Let each host drain its own writes.** Lost, as 0656 found: the hosts share
  one model and a write made in one host must still be applied when the other
  is the one on screen.

## Related

- [0656](0656-draw-changes-as-one-view-hosted-by-the-side-panel-and-the-git-window-sharing-one-commit-draft.md)
  chose the side panel as the drainer; this record moves the draining out of
  every panel.
- [0501](0501-announce-a-write-before-the-editor-sees-it.md) is the
  announcement this module makes.
