# 0674. Mark a branch checked out in another worktree and poll that worktree's changes only while its row is in view

Status: proposed
Date: 2026-11-05

## Context

A repository worked on by several agents has many linked worktrees, each with a branch checked out. A branch held by
another worktree cannot be checked out here, and whether that worktree has uncommitted changes decides whether the
branch can be touched. Nothing tells the window when a file in another worktree changes: the folder watcher covers the
open folder only, so the changes there can only be counted again from time to time, and a status walk of a large
worktree costs real CPU.

## Decision

- **The marker.** A row whose local branch is checked out in another worktree (`WorktreesModel::OtherWorktreeOf`)
  shows a worktree icon, and beside it a dot when that worktree has uncommitted changes, nothing when it has none, "…"
  while it is counted, and nothing when the count failed; the tooltip names the worktree's folder and, after a failure,
  "Cannot count the changes: <message>". The branch's menu has "Show worktree folder". The open folder's own branch
  has the HEAD dot instead, and its changes are the Local changes row.
- **The list** of worktrees is read again for each branch snapshot, on a worker.
- **The poll.** Only while the Git window is on screen, and only the worktrees whose rows were drawn in the last frame,
  are counted. A worktree is counted again 3 s after its last count landed, twice as long each time the count comes
  back the same (or fails again), up to 48 s; a count that changed starts it over at 3 s. Coming back on screen makes
  every count due at once. A count of a worktree that left the list, or of a folder closed meanwhile, is dropped.

## Rationale

Counting only what is in view, and less often what does not change, bounds the cost by the rows on screen rather than
by the number of worktrees, while a worktree being worked on is still seen within seconds.

## Consequences

### Positive

- The idle cost of the window no longer grows with the number of worktrees.
- A failed count says why instead of waiting for good.

### Negative

- A worktree left alone for a while takes up to 48 s to show a change made in it.
- A row scrolled into view shows "…" until its first count lands.

### Neutral

- Worktrees whose folder is missing, and detached ones, have no marker.

## Alternatives Considered

- **Watch every worktree's folder.** One watcher per worktree, each with its own handles and threads, for what a
  bounded poll already answers.
- **Count every worktree every 3 s.** A full status walk of each worktree every 3 s is a steady idle load with the many
  worktrees agents create.

## Related

- [0669](0669-show-references-in-a-sidebar-beside-the-history.md): the rows the marker is drawn on.
- `docs/superpowers/specs/2026-11-03-git-window-lines-design.md` §4.3.
