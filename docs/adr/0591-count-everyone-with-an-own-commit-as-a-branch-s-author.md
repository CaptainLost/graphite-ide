# 0591. Count everyone with an own commit as a branch's author

Status: proposed
Date: 2026-10-15

## Context

The branch list lets a user see their own branches and browse by author. A branch has many commits and several people can commit to it. A branch already merged into the trunk shares every commit with it, so its history says nothing about who the branch is for.

The tip's author alone is cheap to read but names only whoever pushed last. The branch's creator is not recorded by git at all and would need a walk of its own.

## Decision

A branch's authors are the authors of the commits it reaches that the trunk does not.

- The trunk is the default branch of [0589](0589-default-a-review-s-base-to-the-branch-tracking-origin-s-head.md) and its upstream, both hidden from the walk. Without `origin/HEAD` it is, as in 0589, the checked-out branch and its upstream.
- A branch with no such commit counts its tip's author.
- With no trunk at all, as with a detached HEAD and no `origin/HEAD`, every branch counts its tip's author.
- A tip whose commit cannot be read is neither walked nor hidden, so one missing object does not cost every branch its authors. Its branch counts an unknown author.
- Authors are told apart by e-mail, ignoring case, and shown by the name on their newest commit.
- The count stops at 5000 commits per tip.

Core answers it as `Repository::QueryOwnAuthors`: one revwalk over every tip with the trunk hidden, not one walk per branch.

## Rationale

**The tip's author alone loses a branch to whoever pushed last.** A colleague's one-line fix on top of a branch would make it theirs.

**The creator needs a walk too** and leaves a branch with no commits of its own without any author.

**Hiding the upstream as well as the local trunk** keeps commits pulled from a newer `origin/main` from naming everyone who ever committed to main.

Measured in a Release build on a repository with 5000 trunk commits and 2001 branches, warm: `QueryOwnAuthors` took 24 ms and `QueryBranches` 3 ms (first run: 41 ms and 11 ms).

## Consequences

### Positive

- Ticking yourself in the authors list finds a branch a colleague also touched.
- One walk answers every branch.

### Negative

- A merged branch is attributed to whoever made its last commit.
- Without `origin/HEAD` the trunk is whatever is checked out: with a feature branch checked out, trunk commits it lacks count as the other branches' own. Only a detached HEAD without `origin/HEAD` falls back to tip authors.
- `MaxCommitsPerTip` bounds each tip's answer but not the walk: a branch with a long history unrelated to the trunk is read whole, in the background, on every tip move, and on every listing while no `user.email` is set.

### Neutral

- Two people with the same e-mail in different case count as one.

## Alternatives Considered

- **The tip's author only.** Cheapest, but it names whoever pushed last.
- **The creator.** Not recorded; recovering it needs a walk per branch and gives nothing for an empty branch.
- **Any author since the merge base, computed per branch.** The same answer as the decision, at one walk per branch instead of one in total.

## Related

- [0589](0589-default-a-review-s-base-to-the-branch-tracking-origin-s-head.md): the default branch that is the trunk.
- [0002](0002-keep-core-free-of-ide-knowledge.md): `QueryOwnAuthors` names no branch list.
- [0492](0492-give-each-worker-thread-its-own-repository-handle.md): the walk runs on a worker's own handle.
