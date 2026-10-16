# 0598. Delete only the untracked files a deleted folder lists in the changes panel

Status: proposed
Date: 2026-10-15

## Context

The Changes panel offers Delete on an untracked folder, and asks first
([0515](0515-ask-before-every-discard-in-the-changes-panel.md)). The question
counts the files the folder's listing holds, and that listing comes from git
([0514](0514-expand-an-untracked-directory-by-asking-git-about-that-directory-alone.md)),
so it leaves out the files git ignores. An untracked folder can hold such files:
a `secrets.env`, a build directory, a local database.

Delete used to remove the folder whole. The question said "Delete config/ and the
1 files in it?", the panel showed one file, and Delete then took the ignored
files too, permanently. Discarding untracked files on the way to a checkout
already did the opposite and left ignored files alone
([0540](0540-discard-or-stash-every-change-on-the-way-to-a-checkout-untracked-files-only-on-request.md)).

## Decision

Delete on an untracked folder removes the untracked files its listing shows, one
by one, and the directories that removal leaves empty. It never removes a file
git ignores, so a folder that holds one stays, with that file in it. A nested
repository inside the folder is left alone.

The question names what goes: "Delete the 2 untracked files in config/?", or
"Delete the untracked files in config/?" while the listing is not yet known.

Delete and the checkout's discard share one removal, so the two cannot drift
apart again.

## Rationale

The question is the user's only view of what Delete will do. Removing files it
never named breaks that, and the files it leaves out are the ones least likely to
be recoverable, since no history holds them and an ignore rule usually marks them
as local on purpose.

## Consequences

### Positive

- Delete removes exactly what the question counts, and never an ignored file.
- One removal serves Delete and the checkout's discard.

### Negative

- After Delete, a folder that held ignored files is still on disk, which can look
  as if Delete did not work. The folder no longer shows in the Changes panel,
  since nothing untracked is left in it.
- A user who did want the whole folder gone has to delete it outside Graphite.
- Delete on a nested repository removes nothing.

### Neutral

- A single untracked file whose removal leaves its folder empty takes the folder
  with it, as `git clean -d` would.

## Alternatives Considered

- **Remove the whole folder, but count the ignored files in the question.** The
  question would be truthful, but counting means walking every ignored file
  first, which a `node_modules` or build directory makes slow, and it still
  deletes files no history holds.
- **Move the folder to the Recycle Bin.** Makes the delete recoverable, but still
  removes files the question never named, and leaves Delete and the checkout's
  discard with two different rules.

## Related

- [0515](0515-ask-before-every-discard-in-the-changes-panel.md) — the question
  Delete asks.
- [0514](0514-expand-an-untracked-directory-by-asking-git-about-that-directory-alone.md) —
  the listing the question counts.
- [0540](0540-discard-or-stash-every-change-on-the-way-to-a-checkout-untracked-files-only-on-request.md) —
  the checkout's discard, which now shares this removal.
