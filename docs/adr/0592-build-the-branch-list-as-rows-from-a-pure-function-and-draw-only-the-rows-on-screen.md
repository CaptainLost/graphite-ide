# 0592. Build the branch list as rows from a pure function and draw only the rows on screen

Status: proposed
Date: 2026-10-15

## Context

With about 2000 branches every frame measured and drew every row. Sorting by date and counting matches needs every tip anyway, so showing only part of the list does not save the reading.

## Decision

`BuildBranchRows` turns the branches, their authors and the filter into one flat list of rows of one text-line height. It is rebuilt only when an input changes.

`BranchListView` draws the rows through `ImGuiListClipper` in a child that scrolls under a fixed filter box. There is no pagination.

The filter box matches a branch's short name or any of its authors' names or e-mails, and with "Also commit message" its tip's message; every word must match one of them. Until the authors are read, the tip's author stands in, so typing never waits for them. The unknown author's stand-in name matches no word. Grouped by author, a branch sits under those of its shown authors (the ticked ones when any are ticked) that a word matched, or under all of its shown authors when no word matched one.

Sort and grouping are saved with the folder's state; the filter text and the chosen authors are not. A loader appears only after the wait has lasted 0.2 s ([0576](0576-draw-a-loading-indicator-through-a-core-facade-over-imspinner.md)).

Reading the branches costs milliseconds once the listing reads HEAD and the branch configuration once per listing instead of once per branch.

## Rationale

Measured in a Release build on a repository with 2001 branches: `QueryBranches` took 3 ms warm (11 ms first) and `QueryOwnAuthors` 24 ms warm (41 ms first). Reading the tips is cheap; drawing them was the cost. Before the listing read HEAD and the configuration once, it took about 215 ms, nearly all of it in two libgit2 calls repeated per branch.

Paging would sort and count only the loaded part, so the order and the counts would change as pages arrived.

## Consequences

### Positive

- The frame cost no longer depends on the branch count.
- Rows are tested without ImGui.

### Negative

- Every row kind must stay exactly one text line high, or the clipper misplaces rows. A branch row ends on a one-row `Dummy` so that a row with no age, badge or highlighted match is not 2 px taller than the clipper's row height.
- The section headings are tighter than before.
- Reading the branches stays fast only while the listing avoids two libgit2 calls per branch: `git_branch_is_head` re-reads HEAD from disk and `git_branch_upstream` takes a new configuration snapshot on every call, together about 210 ms for 2001 branches. The listing resolves HEAD once and compares reference names; it takes one configuration snapshot and skips `git_branch_upstream` for a branch missing its `remote` or `merge` entry there.

### Neutral

- A list narrowed to one author opens showing everyone again the next time the folder is opened, in the order and grouping it had.

## Alternatives Considered

- **Pagination with "Load more".** Sorting and counting matches would cover only what was loaded.
- **A tree by `/`.** Groups branches by name, not by who works on them, and still draws every row.

## Related

- [0576](0576-draw-a-loading-indicator-through-a-core-facade-over-imspinner.md): the loader's delay.
- [0530](0530-draw-a-frame-only-when-there-is-reason-to.md): frames are drawn only on a reason.
- [0541](0541-put-branch-and-stash-actions-in-the-git-panel-s-right-click-menus.md): the menus the rows carry.

Superseded in part by [0658](0658-open-the-git-window-on-a-navigator-of-work-changes-branches-and-stashes-beside-one-main-area.md): `BranchListView`, `BuildBranchRows` and the saved sort and grouping are gone; the table of every branch ([0657](0657-browse-every-branch-in-a-table-in-the-git-window-s-main-area.md)) builds its rows from a pure function and draws only the visible ones, as this record says.
