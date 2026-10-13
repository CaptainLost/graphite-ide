# 0580. Review a branch against its merge base with the base taken from HEAD

Status: proposed
Date: 2026-10-13

## Context

A branch review answers one question: what does this branch change? The base it is compared with moves on while the branch is open. A plain diff between the two tips also shows every later commit on the base, reversed, as if the branch had removed it.

Most branches are reviewed against the branch the user has checked out, and that is the only base the user has already chosen.

## Decision

A review is the diff from `git merge-base branch base` to the tip of the branch. It never shows the base's later commits.

The base defaults from HEAD:
- for any other branch, the base is the checked-out branch;
- for the checked-out branch itself, the base is its upstream;
- while HEAD is detached, there is no base until the user picks one in the tab.

The tab's base field changes the base to any branch.

A missing branch, a missing base and no common ancestor are answers the tab states in a sentence. They are not failures. The tab of a deleted branch offers Close.

A branch move keeps the last loaded review on screen until the new diff arrives. The tab does not show "Loading…" for each commit.

Core gained `ResolveReference`, `MergeBase`, `WorktreeHolding`, `FileDiff::OldId` and `FileDiff::NewId`, and `LogRequest::Exclude`. Nothing in Core knows about reviews ([0002](0002-keep-core-free-of-ide-knowledge.md)).

## Rationale

**The merge base is the branch's own work.** Whatever the base gained since the fork is not the branch's doing, and a reader of the review should not have to subtract it.

**The base the user already chose is the right first guess.** It costs no click in the common case, and the field is one click away from any other.

**Absence is an answer.** A branch deleted in a terminal while its tab is open is a normal event. A sentence and a Close button serve the reader better than an error.

**No flash per commit.** A review of a branch the user is committing to changes with every commit. Replacing the list with a loading state each time makes the tab flicker and loses the reader's place.

## Consequences

### Positive

- The review matches what a merge into the base would bring in.
- The common case needs no choice.
- Core gains general git queries any feature can use.

### Negative

- Detached HEAD has no default, so the user picks a base before the first review.
- The checked-out branch has no default base when it has no upstream.
- The screen can show a review one move behind until the new diff arrives.

### Neutral

- A diff from the merge base to the tip needs the merge base found first, so each review costs one more lookup than a two-tip diff.

## Alternatives Considered

- **Diff the two tips.** Simpler, but it shows the base's later commits reversed, so the review lists changes the branch never made.
- **Always ask for the base.** Never wrong, but it costs a choice on every review for a value that is nearly always the checked-out branch.
- **Fixed base such as `main`.** Wrong for every repository whose trunk has another name or whose work stacks on a feature branch.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md): the queries added to Core name no review.
- [0581](0581-show-a-branch-s-uncommitted-work-as-a-group-of-its-own-in-its-review.md): uncommitted work in the same tab.
- [0582](0582-key-a-viewed-mark-by-its-group-its-path-and-the-pair-of-blob-ids.md): the viewed marks of a review.
