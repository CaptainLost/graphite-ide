# 0658. Open the Git window on a navigator of work, changes, branches and stashes beside one main area

Status: superseded by 0669
Date: 2026-10-28

## Context

The Git panel ([0493](0493-draw-the-git-view-as-a-centrally-docked-panel.md)) drew the refs down one side and the history of the selected one beside them. What a person does with a branch after a session in its worktree was spread over three places: the side Changes panel, the Git panel and a separate review tab. Nothing said which branches still hold work.

The work now has its own views: cards of the work not yet in the trunk ([0654](0654-count-a-branch-as-holding-work-while-a-commit-of-its-own-has-no-patch-twin-in-the-trunk-or-its-worktree-has-changes.md)), a review embedded in a window ([0655](0655-start-a-branch-review-on-its-first-viewed-mark-and-keep-drafts-out-of-reviews-json.md)), a Changes view with a host in the Git window ([0656](0656-draw-changes-as-one-view-hosted-by-the-side-panel-and-the-git-window-sharing-one-commit-draft.md)) and a table of every branch ([0657](0657-browse-every-branch-in-a-table-in-the-git-window-s-main-area.md)). They need one home, every branch and stash action must stay reachable from the same right-click menus ([0541](0541-put-branch-and-stash-actions-in-the-git-panel-s-right-click-menus.md)), and saved dock layouts must still place the window.

## Decision

`GitView` keeps its panel identity, `git`, and draws a navigator column beside one main area.

- **Navigator.** 15 font sizes wide to start, resizable. From the top:
  - the repository folder's name in bold with its path after it on one line, then HEAD's branch with its distance to its upstream in a chip, in the status bar's marks (`GitStatusText`). While a checkout runs, the line names it with the Core loader beside it, in place of the loader [0635](0635-report-each-git-write-in-the-status-bar-from-the-click-until-the-status-after-it-lands.md) put on the target's branch row, which no list draws any more;
  - `NavigatorView`'s rows, each led by its icon or dot: Work with its count of threads once the facts have landed, up to six threads with their state dot and distance to the trunk, "Show all", the ready and locked counts; Changes with its count; Branches, ruled off, with the six newest local branches and their ages, HEAD's marked with an accent dot, and "Show all";
  - the stashes, with their menu, as before.
- **Main area.** One of five views, `GitWindowView`:
  - **Work**: `WorkView`.
  - **Thread**: `DrawThreadHeader`, led by Back to Work, then the embedded review of the branch. The thread follows a rename of its branch, and falls back to Work when its branch is gone or the review asks to close.
  - **Changes**: the window's `ChangesView` in a column 22 font sizes wide to start, resizable, and the picked file's diff beside it.
  - **History**: the filter bar, the commits and the preview as before, under a header naming the ref, "HEAD · main" for HEAD's. While another ref's history is read, the wait is shown, not the rows of the one shown before.
  - **Branches**: `BranchTableView`.
- **Reviews.** A thread drawn for a branch with neither a session nor a draft opens a draft ([0655](0655-start-a-branch-review-on-its-first-viewed-mark-and-keep-drafts-out-of-reviews-json.md)) against the base `OfferReview` offers, else the trunk. A branch is never drafted against itself: the trunk's own thread, with no upstream to offer, says "There is nothing to review main against.", and with no trunk at all the thread says there is no trunk to review against. A session or draft keeps its own base. The branch menu's "Review", "Review against" and "Open review" open the thread and bring the window forward. A review tab opens only from the thread header's "Open in own tab", which keeps the draft as a session first, because a tab exists only with a session.
- **Review progress.** A card and the thread header show the files viewed out of the files in the committed range, in place of the badge [0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md) put on branch rows. A kept session counts its marks as the badge did; a draft or no session counts none viewed, against the same base its thread would show. The count reads as waiting while the range or its diff is read. A count that will not come, because the range has a problem, the range or its diff could not be read, or the branch has no base, is never drawn as a wait: a card shows "—" with the reason as its tooltip, and the thread header shows no count, since the thread itself explains. A count is held per branch until its range, its marks or its whitespace setting change, so a card does not keep its diff among the review model's few held diffs.
- Selecting a branch or a stash shows its history. File History and Show Commit show HEAD's history, and HEAD's branch row reads as selected.
- Every branch has `RenderRefMenu`'s menu wherever it is shown: the navigator's branch and thread rows, the table, a card, and the header of its history, HEAD's branch on HEAD's. Rows and cards are keyed by what they name, so a menu stays with its branch when the rows or cards around it move.
- **Which view opens.** Work, the first time. After that the view last selected, kept with the folder's state as `restore.gitWindow.view` (a word: `work`, `thread`, `changes`, `history`, `branches`) and `restore.gitWindow.branch`. An unknown word reads as Work. A restored thread or history waits for the branches, then `SelectionToRestore` applies it: a thread of a branch that is gone opens Work, a history of a ref that is gone shows HEAD's.
- **Cost.** The work facts are asked for only while the window is on screen; a branch snapshot it missed is asked for once it is. A read answers every branch before it counts the worktrees' changes, and a first read hands out what it has as it goes, so the cards resolve one by one; a later read replaces the facts only once it is whole, so the cards it had answered never fall back to unchecked meanwhile. A branch with no change of its own skips the trunk walk, and a walk stops once every change of the branch has found its twin. On this repository's own (36 worktrees, 34 local branches, 2210 commits, Release build), the first read in a fresh process takes 11 s, of which every branch is answered after 2.4 s and the rest is the 35 worktree counts, about a quarter of a second each, most of it libgit2 reading the submodules' working trees; a second read in the same process takes 9 s, every branch answered after 0.2 s. Before the trunk walk was skipped and stopped early, the first read took 60 to 90 s and a second 10 to 20 s, with every branch an unchecked card until the end. The card worktrees' changes are polled only while Work is shown. The Claude sessions, whose titles the cards take, are indexed once the window is on screen, also when the Sessions panel was never shown. The board, the navigator rows and the table's rows are rebuilt only when their inputs change, and the snapshots they were built from are held, so a freed snapshot can never pass for a new one at the same address.
- **The ready group only reads.** Nothing in this window removes a branch or a worktree. Before a later stage acts on the ready group, it must:
  - act only while the facts are not pending;
  - add the trunk's tip to the check of what moved since the facts were read, which compares branch tips only, so a trunk reset or force-moved keeps showing entries read against the old trunk until the next read lands;
  - read the worktree list again when it acts, not trust the one the facts were read with;
  - weigh locks and live sessions: a worktree locked by another session, or one a Claude session is still running in, is not removed on the facts alone.
- **One working-tree diff.** A pick in the window's Changes closes a working-tree diff tab of another diff, and a working-tree tab opened after the pick takes the slot back, as the side panel's tabs do ([0538](0538-refresh-the-working-tree-diff-when-the-repository-changes-not-when-it-is-asked-for.md)).
- The sectioned branch list (`BranchListView`, the row building in `BranchRows`) and its saved sort and grouping are deleted. The table's columns, scopes and filter take their place.

## Rationale

**A navigator and one main area.** The navigator keeps every section and its count in sight, and the main area gives whichever view is open the room a history needed alone before. Each view stays its own class; the frame only chooses one.

**Work first, then where the reader left.** Work is what waits after a session, so a first opening shows it. A person who works in History or Changes comes back to it, so the last view is kept. A branch that went away meanwhile is not an error: the view falls back to the nearest thing that still exists.

**Facts only while seen.** A facts read lists every worktree and counts the changes in each, and branch tips move with every commit any session makes. Reading them for a window nobody looks at costs disk and CPU for nothing.

## Consequences

### Positive

- One window holds the work, the changes, a review, a branch's history and every branch, and every action of [0541](0541-put-branch-and-stash-actions-in-the-git-panel-s-right-click-menus.md) is still in its menu.
- Saved dock layouts still place the window, which kept its identity.
- Reopening a folder shows the view it was left on.

### Negative

- History no longer sits beside a full branch list. Only the six newest local branches are one click away; any other is two, through "Show all" and the table.
- The branch list's saved sort and grouping are dropped from the folder's state, and nothing carries them into the table, whose own sort, scope and filter are not saved.
- A window brought back after commits elsewhere shows its facts as pending until the read it then asks for lands.
- A change to the worktree list that moves no branch is seen only on the next branch move or Refresh: removing a worktree but keeping its branch, locking or unlocking one, `git worktree prune`. Meanwhile a card can keep "Open folder" for a folder that is gone, since a count that fails to poll leaves the one held.
- A card whose review cannot be counted says why only in the tooltip of its "—"; the thread says it in full.
- Every card on screen costs a range and a diff on first sight, with a session or without one, and its range is read again after every branch snapshot while it is drawn.
- `GitView` owns the state of five views. It is split across files by part (`GitView.cpp`, `GitViewFrame.cpp`, the dialogs, the stashes, the reveal), but it is one class.

### Neutral

- The frame's table is named `gitWindow`, not `git`, so the navigator opens at its own width instead of the width ImGui saved for the old refs column.
- A selected stash is not kept; a restored history names a ref or HEAD.

## Alternatives Considered

- **Tabs across the top of the window, one per view.** Simpler to draw, but the counts, the threads and the newest branches the navigator shows at a glance would each be a click away.
- **A panel per view.** Work, Branches and History as panels of their own would be more places to look and more dock layout to keep, where the point was one place.
- **Always open on Work.** Simpler, and nothing to restore, but a person who left the window on History would land elsewhere every time.

## Related

- [0493](0493-draw-the-git-view-as-a-centrally-docked-panel.md): the panel the frame is drawn in.
- [0541](0541-put-branch-and-stash-actions-in-the-git-panel-s-right-click-menus.md): the menus every branch and stash keeps.
- [0592](0592-build-the-branch-list-as-rows-from-a-pure-function-and-draw-only-the-rows-on-screen.md): the branch list this replaces, whose way of building rows the table keeps.
- [0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md): the review badge the cards' progress replaces.
- [0635](0635-report-each-git-write-in-the-status-bar-from-the-click-until-the-status-after-it-lands.md): the checkout's loader, moved from the branch row to the navigator header.
- [0654](0654-count-a-branch-as-holding-work-while-a-commit-of-its-own-has-no-patch-twin-in-the-trunk-or-its-worktree-has-changes.md), [0655](0655-start-a-branch-review-on-its-first-viewed-mark-and-keep-drafts-out-of-reviews-json.md), [0656](0656-draw-changes-as-one-view-hosted-by-the-side-panel-and-the-git-window-sharing-one-commit-draft.md), [0657](0657-browse-every-branch-in-a-table-in-the-git-window-s-main-area.md): the views the frame hosts.
