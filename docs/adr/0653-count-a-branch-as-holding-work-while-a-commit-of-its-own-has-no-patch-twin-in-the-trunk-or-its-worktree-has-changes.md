# 0653. Count a branch as holding work while a commit of its own has no patch twin in the trunk or its worktree has changes

Status: proposed
Date: 2026-10-28

## Context

The Git window opens on the work in progress: one card for each line of
work that has not reached the trunk, and a separate group for branches that
can be removed. A branch that is merged by the book is easy to tell: its tip
is an ancestor of the trunk. Most branches in a busy repository are not
merged that way. They were squashed, rebased or cherry-picked, so the trunk
holds their changes under other commit ids, and a check by ancestry would
call every one of them unfinished.

Core can already answer the question by patch id (`Core::PatchMatch`): of a
branch's commits, how many carry a change of their own, and how many of those
have a twin among the trunk's commits. That answer can be incomplete when the
budget ran out, can fail for one branch, and arrives later than the branch
list. A branch can also be checked out in a worktree with files not yet
committed, which no commit comparison sees.

## Decision

`BuildWorkBoard` (`Graphite/src/Git/Work/WorkBoard.h`) is a pure function of
the branch list, the work facts, the working copy's change count and a title
lookup. A branch holds work, and gets a card, when any of these is true:

- it is unchecked: there are no facts for it, its read failed, or the patch
  check did not finish (`Complete == false`);
- `OwnChanges > InBase`: a commit of its own has no patch twin in the trunk;
- its worktree has uncommitted files, or the count is unknown for a folder
  that is present.

Every other branch goes to the ready group, as Merged when `Ahead == 0` or
none of its commits carries a change of its own (merges and empty commits do
not), otherwise as SameChangeInTrunk. A branch in a locked worktree is never
offered for removal: it becomes an information row carrying the lock reason.

The trunk and HEAD's branch get no card and no ready entry of their own;
HEAD's branch is shown by the working copy's card, which is always first. That
card is the open folder's own worktree, a linked one when a linked worktree is
open; any other worktree, the main one included, holds its branch's card. A
detached linked worktree is a card when it has changes, an unknown count or an
operation in progress, and an information row otherwise. A worktree whose
folder is gone is an information row. Facts count as current only when they
answer the request that the present branch list would make
(`WorkFactsRequestFor`, which does not compare the budget, since the board has
none); stale facts are still used, and the board says they are pending.

## Rationale

Unfinished work is worth losing a branch over, so every doubt is resolved
toward showing a card: an unchecked branch, an incomplete check and an
uncountable worktree all keep a branch out of the ready group. A patch twin
is what survives a squash or a rebase, so counting by patch id rather than by
ancestry puts those branches in the ready group where they belong. Keeping
the rules in a function of plain values lets each be tested without a
repository.

## Consequences

### Positive

- Squashed, rebased and cherry-picked branches are offered for removal
  without hiding any branch that still has a change of its own.
- Nothing is called ready on a guess: a failed or unfinished check, or an
  uncountable worktree, shows as a card.
- The board is built from values already read, so redrawing never waits on
  git.

### Negative

- A branch whose change was altered while being brought into the trunk has no
  patch twin and keeps its card until it is removed by hand.
- With a small budget many branches stay unchecked and keep cards.
- Stale facts are shown rather than dropped, so a card can be briefly wrong
  until the new answer lands.

### Neutral

- Ordering is by tip time: the working copy, branch cards newest first, then
  detached cards.

## Alternatives Considered

### Decide by ancestry alone

Ask only whether the tip is reachable from the trunk. Rejected: it leaves
every squash-merged branch looking unfinished, which is most of them.

### Hide branches whose check did not finish

Treat an incomplete answer as no work. Rejected: it could offer a branch with
unmerged commits for removal.

## Related

- [0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) - git
  data comes from Core, and freshness is Graphite's to manage.
