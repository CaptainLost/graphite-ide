# 0670. Take a branch's review base from its line

Status: proposed
Date: 2026-11-04

## Context

[0589](0589-default-a-review-s-base-to-the-branch-tracking-origin-s-head.md) named a trunk: the local branch tracking `origin/HEAD`'s target, else the checked-out branch. Reviews, the history's fork separator and the "own" authors of a branch all hung on that one branch. The line model ([0663](0663-compare-each-branch-with-the-line-it-forked-from.md) onward) reads every branch's line from the commit graph: the bases of the repository (several, when it keeps several long-lived branches) and, for each other branch, the base it is compared with. A single trunk is a guess the line read no longer needs, and it is wrong for a repository with more than one base.

## Decision

- A branch's review base is its line: the ref it is compared with, as `BranchLines::ComparedWithName` says. The review item reads `Review against <short name of the line>`.
- A base is reviewed against its upstream, and not at all without one, as 0589 did for the trunk.
- A branch compared with nothing has no review base. The message says so and what to do: `<branch> is compared with nothing, so there is nothing to review it against. Pick a line with Compare with… or use Review against.`
- Until the first line read lands there is no line to take: no review is offered, and the `Review against` submenu still lists every other local branch.
- The history's fork base is the same base a review would take, so the history and the review agree on where a branch begins.
- The "own" authors of a branch leave out the commits of every base: `GitModel::SetAuthorsHidden` takes the tips of the read's bases, and the authors are not asked until it has been given them, since with nothing hidden every commit would count as the branch's own.
- `Graphite/src/Git/DefaultBranch.*` goes. The three plain lookups it held (`BranchWithFullName`, `RemoteBranchNamed`, `CheckedOutBranch`) move unchanged to `Git/Branches/BranchLookup.*`.

## Rationale

The line read already answers "what is this branch a line of" from the graph, for every branch at once. Reading it again through a second, name-and-remote-HEAD rule would give two answers to one question, and the second would be right only in repositories with one base.

## Consequences

### Positive

- A review, the history's separator and the sidebar's `vs` label name the same base.
- A repository with several bases reviews each branch against its own.
- A repository without `origin/HEAD` needs no fallback to the checked-out branch.

### Negative

- Nothing is offered before the first line read lands, a moment after the branches do.
- A branch the read compares with nothing cannot be reviewed from the menu's first item; the user picks a base.

### Neutral

- A session keeps the base it started with ([0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md)).

## Alternatives Considered

- **Keep the trunk rule beside the lines.** Two sources for a branch's base that disagree on every repository with more than one base.
- **Fall back to the trunk where the line is empty.** Hides the cases the line model names ("compared with nothing") behind a guess.

## Related

- [0589](0589-default-a-review-s-base-to-the-branch-tracking-origin-s-head.md): superseded by this record.
- [0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md): the session that keeps the base.
