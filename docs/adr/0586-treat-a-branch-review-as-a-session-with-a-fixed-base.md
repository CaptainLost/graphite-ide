# 0586. Treat a branch review as a session with a fixed base

Status: proposed
Date: 2026-10-14

## Context

A review of a branch is not one sitting. The reader marks some files, closes the tab, comes back after the branch gained a commit, and wants the earlier marks and settings where they left them. A tab cannot hold that: it is gone when it is closed.

The first review tab let the reader change its base at any time ([0580](0580-review-a-branch-against-its-merge-base-with-the-base-taken-from-head.md)). The marks ([0582](0582-key-a-viewed-mark-by-its-group-its-path-and-the-pair-of-blob-ids.md)) are only meaningful against one base: change it and every file's diff, and so every mark, may change under the reader.

The Git window already lists the branches, so it is where the reader sees which of them are under review.

## Decision

A review is a session: one branch, its base, its settings and its marks. A session outlives its tab; closing the tab pauses it.

- The base is chosen once, when the session starts, and never changes. Ending the review and starting another is the way to a different base.
- The Git window's branch menu starts, opens and ends sessions. A branch without a session offers "Review against" its default base ([0580](0580-review-a-branch-against-its-merge-base-with-the-base-taken-from-head.md)); with HEAD detached, a submenu of the local branches. A branch with a session offers "Open review" and "End review…". Ending asks first, then deletes the session with its marks and closes its tab.
- A branch under review shows a badge in the Git window: the viewed and total files of the committed range, "merged" when the branch is already in its base, and a question mark when the range has a problem. It is worked out only for the rows on screen, and again only when a branch tip moves.
- A tab closes when its session is gone.
- A branch renamed from Graphite takes its session with it, and sessions whose base was that branch follow the new name.
- The session of a branch that is deleted elsewhere is dropped once two consecutive branch snapshots lack it. One snapshot is not enough: a snapshot can be taken in the middle of a change.
- Sessions are kept in `reviews.json` ([0583](0583-keep-review-marks-in-their-own-file-beside-the-window-state.md)).

## Rationale

**The reader's work belongs to the branch, not to a tab.** Pausing on close means the marks and settings are still there when the reader returns.

**A fixed base keeps the marks honest.** A mark records a pair of blobs; a changed base changes the pair. Fixing the base removes the case where the list rewrites itself under the reader's hand.

**The Git window is where branches are.** Starting a review from a branch, and seeing its progress beside it, needs no trip to a separate list of reviews.

**Two snapshots before dropping.** A branch missing from one snapshot may be a refresh caught mid-update. Dropping its session would lose the reader's marks for good.

## Consequences

### Positive

- Closing a tab loses nothing.
- Progress on every branch under review is visible without opening it.
- A review cannot silently change what it compares.

### Negative

- A base picked wrongly is only fixed by ending the review, which deletes its marks.
- A branch deleted in a terminal keeps its session until the second snapshot without it.
- Every branch row on screen with a session costs a range and a diff on first sight.

### Neutral

- The tab no longer has a base field; the base is shown as text.

## Alternatives Considered

- **A base that can be changed in the tab.** Flexible, but marks and diffs then depend on a setting that can change at any click.
- **Tie the session to the tab.** Simpler, but closing the tab would then discard the marks, or leave marks without an owner.
- **A separate list of reviews.** One more place to look; the branch list already has every branch under review in it.
- **Drop a session on the first snapshot without its branch.** Simpler, but loses the marks to a transient snapshot.

## Related

- [0580](0580-review-a-branch-against-its-merge-base-with-the-base-taken-from-head.md): how the base is chosen.
- [0582](0582-key-a-viewed-mark-by-its-group-its-path-and-the-pair-of-blob-ids.md): the marks a session holds.
- [0583](0583-keep-review-marks-in-their-own-file-beside-the-window-state.md): where sessions are kept.
- [0587](0587-show-a-changed-file-s-diff-since-the-version-that-was-viewed.md): a setting of the session.
