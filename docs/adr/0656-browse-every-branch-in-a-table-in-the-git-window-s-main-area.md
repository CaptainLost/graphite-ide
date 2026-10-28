# 0656. Browse every branch in a table in the Git window's main area

Status: proposed
Date: 2026-10-28

## Context

The navigator beside the Git window lists the newest branches and no more. A repository can hold thousands, and a person looking for an old one, a remote one nobody checked out, or the ones to delete needs every branch at once, with enough beside each name to tell them apart: where it lives, how far it is from the trunk, how old it is, who wrote it.

## Decision

"Show all" opens `BranchTableView`, a table of every branch in the main area.

- `BuildBranchTable` makes the rows from the branches, their authors, the work facts, the branches held by a worktree other than the open folder's, a scope, a sort and a filter. It is a pure function. The view rebuilds rows only when one of those changes, ages being read in minutes, and draws the visible ones through `ImGuiListClipper` ([0592](0592-build-the-branch-list-as-rows-from-a-pure-function-and-draw-only-the-rows-on-screen.md)).
- A local branch and its upstream are one row, named as the local branch. A remote branch no local branch tracks is a row of its own, named as the remote branch. A row's age is the newer of its two tips.
- Five scopes, each shown with its row count under the filter: All; Local, the rows with a local branch; Remote only, the rows without one; Mine, the rows with a commit of the user's; Stale, the rows whose newest tip is 90 days old or more (`kStaleAfter`). The filter takes words matched against the name or an author, as the navigator's does.
- Columns: Branch, Where ("local", the remote's name, or "local · origin", then " · worktree" when a worktree other than the open folder's holds the branch, the main checkout too when a linked worktree is open), Trunk, Age, Authors, Subject. Every column sorts; the default is Age, newest first.
- Trunk is the branch's distance to the trunk from the work facts, shown once the facts answered it. A local branch waits with a loader until then, and a row the facts do not cover (the trunk, a remote branch) shows a dash. HEAD's branch is in the facts request for this column, though the board gives it no card of its own, so it shows its distance too: open in a linked worktree, it is the branch holding the work.
- Clicking a row opens the branch (its local branch when there is one); the right-click menu is the navigator's.

## Rationale

**One row per branch.** Showing a local branch and its upstream twice would double the count and hide that they are the same work. The Where column keeps what the pair says.

**90 days.** Three months is long enough that a branch left alone is no longer in progress, and short enough to list what is worth a look. It is a constant rather than a setting; nothing needs it to move yet.

**Scopes with counts.** The counts tell whether a scope is worth opening, and applying the filter to them keeps the numbers in step with the rows.

## Consequences

### Positive

- Every branch can be found, sorted and filtered in one place, and the frame cost does not depend on how many there are.
- The rows are tested without ImGui.

### Negative

- The view compares its input's branches, authors and facts by address, so a caller that changes their contents in place would see stale rows. Callers hand it new objects.
- A merged row shows one name, the local one; a remote branch of another name that the local branch tracks is visible only in the Where column.
- Mine and the Authors column rely on the authors having been read; until then they fall back on the tip's author.

### Neutral

- The scope, the sort and the filter are not saved. ImGui keeps no settings for the table either: sorted and never resized, the vendored ImGui saves the sorted column alone and on the next start draws it first.

## Alternatives Considered

- **A longer list in the navigator.** It would repeat the navigator's few columns and still hide the remote branches and the distances.
- **One row per ref.** It would list a branch and its upstream apart, and a repository with many remotes would show every name several times.

## Related

- [0592](0592-build-the-branch-list-as-rows-from-a-pure-function-and-draw-only-the-rows-on-screen.md): rows from a pure function, only the visible ones drawn.
- [0541](0541-put-branch-and-stash-actions-in-the-git-panel-s-right-click-menus.md): the menu the rows carry.
