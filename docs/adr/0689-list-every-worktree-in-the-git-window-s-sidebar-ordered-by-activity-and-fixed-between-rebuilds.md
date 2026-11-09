# 0689. List every worktree in the Git window's sidebar, ordered by activity and fixed between rebuilds

Status: proposed
Date: 2026-11-09

## Context

Beside the folder open in Graphite a repository often has many linked worktrees: one an agent is working in right
now, others left from earlier work, made by an agent, by hand in a terminal or by another tool, inside
`.claude/worktrees` or anywhere else, on a branch or on a detached HEAD. Before this record the Git window only marked
a branch held by another worktree with an icon and a dot (ADR 0674); a worktree on a detached HEAD did not appear at
all, and seeing what any of them held meant switching Graphite to that folder.

Three forces shape the list. A repository here had 37 worktrees, so whatever the list costs while idle must not grow
with their number (ADR 0674's bound). The Git window's rule is that nothing moves on its own while the user looks at it
and that its first opening waits behind one state rather than filling in piece by piece (spec 2026-11-03 §6). And the
cheap facts about a worktree (its HEAD, its newest reflog entry and its index time) are known without a status walk, while its count of
changed files needs one.

## Decision

- **The section.** The Git window's sidebar has a section **Worktrees · N** after Yours and before Bases, open by
  default and collapsible, present only when the repository has a linked worktree; N counts every worktree with the
  open folder. The open folder's row comes first and reads `<folder> · this folder`; when the open folder is itself a
  linked worktree, the main checkout is an ordinary row. Each row shows the folder name (full path in the tooltip), the
  branch or `detached at <short hash>`, `N changed` or `clean`, and the time of the last activity; a locked worktree
  shows a lock with its reason, a missing folder shows `folder missing` instead of the counts. The sidebar's search
  also matches a worktree's folder name and path.
- **Order.** Below the open folder, rows are ordered by last activity, newest first: the newest of the time of HEAD's
  newest reflog entry, the index's modification time, the HEAD commit's time, and the newest changed file found by the
  last count. The reflog is read by its entries, not by the file's modification time: git's reflog expiry rewrites every
  worktree's `logs/HEAD` in the same second without adding an entry, which made all of them look active at once.
  Worktrees idle for 7 days with no known changes fold into one closing row `N more · no changes for 7+ days`; a folded
  worktree found with changes is said in place (`N more · 1 with changes`) and leaves the fold at the next build.
- **Fixed between rebuilds.** The order is built at an opening of the window, a change of the search, the section
  opened or closed, a worktree added or removed, and once when the first count settles. Between those moments counts,
  times and the live marker change in place and no row moves.
- **First build.** Until the worktree list has answered (with a list or a failure), the sidebar's sections built from
  the lines (Yours, Bases and those below) stay behind its one waiting row, so the Worktrees section never arrives
  between Yours and Bases after they were drawn and pushes them down. The first build then shows one row `Reading N worktrees…`
  while every worktree is counted once, in parallel on the job queue and without walking into submodules, for at most
  3 s on screen; a worktree not counted by then is placed by its cheap activity and shows "…" until its count lands.
  Once settled, the folder stays settled: a worktree added later, or a list read again, does not bring the waiting
  row back.
- **Counting.** Rows in view keep ADR 0674's poll (3 s, doubling to 48 s while unchanged). Every other worktree, scrolled
  away or folded, is counted by a sweep while the window is on screen: one worktree at a time, at most one count every
  10 s in total, the longest-uncounted first. Counts of the section never walk into submodules.
- **The worktree looked at.** Selecting a worktree row, or a branch row whose branch another worktree has checked out,
  shows that worktree's history: a branch's as for any branch, a detached HEAD's from its commit titled
  `HEAD in <folder>`. Its first row is `Changes in <folder>` with `n staged · m not staged` or `no changes`. That one
  worktree is watched (the review tab's `WorktreeWatch`, plus its linked git directory, where another process's stage
  or commit lands) while the window is on screen; it is neither polled nor swept, and each watch firing counts it again
  so its row keeps its count, activity and live marker. A HEAD that watch reads which the list does not hold (detached,
  on another branch, or a detached HEAD at another commit) asks the worktree list again, since no ref of the open
  folder moves for it; a branch opened from a worktree's row then follows that worktree to its new HEAD, while one
  opened from Yours stays on the branch. Off screen the window keeps following it but stops watching and
  reading, so coming back shows the last state at once instead of "…".
- **The live marker** is a static green dot in place of the folder icon while the last activity is within 60 s. It
  does not pulse.
- **Selection.** A detached worktree's selection is stored per folder by path (`restore.gitWindow.worktree`), a branch
  worktree's as the branch. A selected detached worktree that leaves the list falls back to the open folder's HEAD; a
  branch worktree that leaves it keeps showing its branch, which still exists.

## Rationale

Ordering by activity puts the worktree an agent is busy in on top without knowing anything about agents, and fixing
the order between named moments keeps that from turning into a list that reshuffles under the cursor. A sweep bounded
by time rather than by worktree keeps the idle cost of 37 worktrees what it is for three, while the one worktree the
user is actually looking at is kept fresh by a watch instead of a faster poll. A static dot gives the same at-a-glance
"recent" without asking for a frame every few milliseconds for as long as an agent works.

## Consequences

### Positive

- Every worktree, including detached ones and ones outside `.claude/worktrees`, is one click away from its history and
  its uncommitted changes.
- The idle cost of the section is bounded by time: rows in view on ADR 0674's schedule, one sweep count per 10 s, one
  watch.
- No row moves while the user looks; the first opening fills in behind one waiting row.

### Negative

- A worktree out of view can be up to (number of worktrees × 10 s) out of date; with 37 that is about six minutes.
- The sidebar's sections wait for the worktree list on the first opening of a folder, even when the repository has no
  linked worktree.
- A static dot does not say "changing right now" as a pulse would; it says "changed within the last minute".
- A detached worktree's history has no vs button comparing it with its line (spec §5 asks for one); it is left for a
  follow-up.

### Neutral

- The open folder's own row has no time or live marker; its changes are the Local changes row.
- The branch-row marker in Yours (ADR 0674) stays as it was.

## Alternatives Considered

- **Order by name or by path.** Stable without any rule, but the worktree being worked in sinks among dozens of idle
  ones, which is what the section is for.
- **Re-sort as counts land.** Always correct, and moves rows under the cursor, against the window's rule.
- **Watch every worktree.** One watcher per worktree, with its handles and threads, for what a time-bounded sweep
  already answers; and a watch alone misses a stage or commit, which touches only the linked git directory.
- **A pulsing live marker** (the spec's wording). Rejected for idle CPU: a pulse keeps the window drawing for hours
  while an agent works.

## Related

- [0674](0674-mark-a-branch-checked-out-in-another-worktree-and-poll-that-worktree-s-changes-only-while-its-row-is-in-view.md):
  the poll of rows in view, amended by this record with the sweep and the first-build cap.
- [0669](0669-show-references-in-a-sidebar-beside-the-history.md): the sidebar this section joins.
- [0690](0690-write-into-another-worktree-only-when-it-is-as-the-user-saw-it.md): the writes from the changes row.
- [0691](0691-attribute-a-file-to-the-deepest-worktree-that-holds-it.md): files opened from a worktree.
- `docs/superpowers/specs/2026-11-08-worktree-preview-design.md` §4 (the section, order, folding, counting), §5 (a
  worktree's history and changes row), §8 (live updates), §9 (edge cases).
- `Graphite/src/Git/Worktrees/WorktreesModel.h`, `WorktreeListing.h`, `LookedAtWorktree.h`.
