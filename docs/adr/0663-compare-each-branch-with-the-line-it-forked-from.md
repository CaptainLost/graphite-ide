# 0663. Compare each branch with the line it forked from

Status: proposed
Date: 2026-11-03

## Context

The Git window has to describe a repository with thousands of branches and hundreds of authors, and has to say for
each branch what it is compared with: how many commits it has that the other side lacks, and whether it is merged. The
earlier answer was one trunk for the whole repository ([0654](0654-count-a-branch-as-holding-work-while-a-commit-of-its-own-has-no-patch-twin-in-the-trunk-or-its-worktree-has-changes.md)
counts against it; [0589](0589-default-a-review-s-base-to-the-branch-tracking-origin-s-head.md) takes a review's base
from `origin`'s HEAD). That is wrong whenever work is not built on the trunk. A backport is built on a release line, a
second piece of work is built on the first, and in a fork the trunk of record is another remote's.

A rule that decides the line by branch name, by the number of branches above a commit, or by the length of a path was
tried on real repositories and was right in one and wrong in another.

## Decision

Compare each branch with the line it forked from, found from the commit graph alone (`Core::ComputeLines`).

- **Paths.** Take the first-parent chain of every branch tip; together they form a tree.
- **Weight.** Each commit on the tree gets the number of distinct author e-mails along its heaviest continuation. At a
  fork the continuation is the child with the most distinct authors; ties go to the child with more tips above it,
  then to the newer commit.
- **Split into lines.** From the root up, the continuation keeps its parent's line and every other child starts a new
  line. Two overrides: the first-parent chain of every base always continues, and a line never continues past a base's
  tip; and at another branch's tip the line stops when the line below has three or more authors and the work above has
  fewer than two, because that work is built on the busy line (a stack, an unmerged cherry-pick) and is not the line
  going on.
- **A branch's line.** Walk down from its tip along its own line. The nearest other branch tip below it, other than a
  copy at the same commit, is what it is built on. Otherwise, at the bottom of its line, it joins another line: a
  base's line gives that base; any other line gives the lowest branch tip at or above the join point. A branch whose
  tip lies on a base's line has no own commits.
- **Same row.** A local branch and its upstream with the same short name are one row. A branch whose same-row partner
  is a base compares with that base before any walk, in both directions (a local copy of a base belongs in the base's
  row). A diverged local and upstream pair never compares with itself: the branch takes its partner's comparison.
- **The user's comparison.** It wins. A comparison naming a missing ref, or a ref whose tip the graph lacks, cannot be
  honoured and is ignored: the automatic rule above decides.
- **Counts and merged.** Ahead is the commits reachable from the branch and not from its line. A branch is merged by
  history when its tip is reachable from its line's tip. A branch that only points into the line, that is, whose tip
  is on the line's first-parent chain, has no own commits, which is its own state: a fresh branch is never shown as
  merged. "On the line" always means that the line's first-parent chain holds the tip, so a branch built on another
  sits above that branch's tip.
- The walks are iterative and linear: 200,000 commits in one chain and thousands of branches forking at one commit
  must neither overflow the stack nor go quadratic.
- Names are carried and compared for equality only. The user may override any branch's line; the override wins and is
  stored with the repository's answers ([0665](0665-keep-each-repository-s-git-answers-and-caches-in-local-user-data-keyed-by-its-shared-git-directory.md)).

This supersedes the single trunk of 0654 and the origin-only review base of 0589 for the Git window and for the
callers that used them. It is a partial supersession, and the house practice for one is to say so in the new record
and leave the old records' statuses alone (both are still proposed); no status line was changed.

## Rationale

Authors tell a line that goes on from work that is only built on it: a trunk or release line is touched by many
people, a stack by one. In the spike, continuation by tips above and by path length each scored 91 percent on release
backports in one repository and 20 percent in another, and the reverse in the other; authors scored 86 and 100. A
rule from data that does not depend on how a team names or merges its branches is the only one that holds for every
team.

## Consequences

### Positive

- A backport is compared with its release line, a stack with the branch under it, and the window can say which.
- The model needs the commit graph and nothing else, so it runs in memory in well under a second on 200,000 commits.

### Negative

- Where one author's work is the only work on a quiet line, the author rule can read a stack as the line going on, or
  the reverse. The user's override is the remedy, and the window must say what a branch was compared with.
- A branch built on a stale stack is compared with that branch, not with the base it will eventually land in.

### Neutral

- Merged by patch equivalence and by content is a later, background stage and does not change this decision.

## Alternatives Considered

- **One trunk per repository.** Lost: it counts every backport and stack against the wrong line.
- **Continuation by tips above, or by path length.** Lost: right in one repository and wrong in another.
- **A merge-base against every base, nearest wins.** Lost: it needs a pairwise comparison per branch and base, which
  does not scale to thousands of branches, and it cannot tell a stack from a line.

## Related

- [0664](0664-find-bases-from-server-defaults-merged-lines-and-authors-never-from-names.md) finds the bases the lines
  are split around.
- [0654](0654-count-a-branch-as-holding-work-while-a-commit-of-its-own-has-no-patch-twin-in-the-trunk-or-its-worktree-has-changes.md)
  and [0589](0589-default-a-review-s-base-to-the-branch-tracking-origin-s-head.md) are superseded in part.
- `docs/superpowers/specs/2026-11-03-git-window-lines-design.md`, sections 5.1 and 5.2.
