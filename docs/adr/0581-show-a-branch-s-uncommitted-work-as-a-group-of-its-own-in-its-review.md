# 0581. Show a branch's uncommitted work as a group of its own in its review

Status: proposed
Date: 2026-10-13

## Context

A review reads the committed range of a branch ([0580](0580-review-a-branch-against-its-merge-base-with-the-base-taken-from-head.md)). The author of that branch often has edits not yet committed, in the worktree that holds it, and wants to see them beside the committed work. Mixing the two in one diff would make a line's place in history unclear. It would also change a file's diff on every save to disk.

Watching a worktree and diffing it costs git work, and Graphite draws only when there is reason to ([0530](0530-draw-a-frame-only-when-there-is-reason-to.md)).

## Decision

Uncommitted work is off by default. A toggle in the tab turns it on. The toggle is disabled, with a tooltip, for a branch no worktree holds.

When it is on, the worktree holding the branch is diffed against its HEAD, which is the tip of the branch. Those files are listed under their own heading, "Uncommitted in worktree". Committed and uncommitted work are never mixed in one diff. An untracked file counts as an added file ([0504](0504-treat-an-untracked-file-as-an-added-file-in-a-working-tree-diff.md)).

The worktree is watched only while the toggle is on and the tab is open. Changes are debounced. A tab that is hidden does no git work. The worktree's diff is asked again when the watch reports a settled change and when the branch's tip moves, since a commit made in the worktree changes no file the watch reports. A branch list that leaves the tip where it was asks for nothing.

The worktree is opened at exactly its folder. A folder that lost its `.git` file reads as gone, never as the repository around it, and a worktree whose folder is gone holds no branch.

When a refresh arrives and the selected file's pair of blob ids is unchanged, its texts carry forward and are not read again. A working-tree side that libgit2 left unhashed is hashed through the repository's filters, so the pair is comparable.

## Rationale

**Two groups answer two questions.** What the branch has done and what the author is still doing are different things to read. A heading says which is which.

**Off by default keeps the cost off.** Most reviews are of work that is done. They need no watcher and no worktree diff.

**A hidden tab does no git work.** A tab nobody sees has no reason to run git, so its refreshes wait until it is drawn. With a review tab open and nothing changing, the instance used 0.6 s of CPU over 60 s.

## Consequences

### Positive

- The reader can tell committed from uncommitted work at a glance.
- An idle review tab draws and works almost nothing.
- An unchanged file is not read again on every refresh.

### Negative

- A branch no worktree holds has no uncommitted work to show, and the toggle says so instead of doing nothing.
- A file edited in both groups appears twice, once in each.

### Neutral

- The uncommitted group is diffed against the tip, not against the base, so it never repeats committed changes.

## Alternatives Considered

- **One diff of base to worktree.** Shows everything in one list, but a file's committed and uncommitted lines blend, and each save changes the whole file's diff.
- **Always include uncommitted work.** Makes every review pay for a watcher that most do not need.
- **Poll the worktree on a timer.** Redraws and runs git with nothing changed, against [0530](0530-draw-a-frame-only-when-there-is-reason-to.md).

## Related

- [0580](0580-review-a-branch-against-its-merge-base-with-the-base-taken-from-head.md): the committed range.
- [0504](0504-treat-an-untracked-file-as-an-added-file-in-a-working-tree-diff.md): how an untracked file is shown.
- [0530](0530-draw-a-frame-only-when-there-is-reason-to.md): why a hidden tab stays quiet.
- [0582](0582-key-a-viewed-mark-by-its-group-its-path-and-the-pair-of-blob-ids.md): marks per group.

Amended by [0655](0655-start-a-branch-review-on-its-first-viewed-mark-and-keep-drafts-out-of-reviews-json.md): uncommitted work is no longer off by default. A draft, and a session started on a branch that had neither, open with it on, since the Git window's thread shows the uncommitted group by default; the toggle still turns it off, and the worktree is still watched only while it is on and the review is drawn.
