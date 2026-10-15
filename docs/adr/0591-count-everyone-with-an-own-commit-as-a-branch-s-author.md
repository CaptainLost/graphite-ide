# 0591. Count everyone with an own commit as a branch's author

Status: proposed
Date: 2026-10-15
## Context

The branch list lets a user see their own branches and browse by author. A branch has many commits and several people can commit to it. A branch already merged into the trunk shares every commit with it, so its history says nothing about who the branch is for.

The tip's author alone is cheap to read but names only whoever pushed last. The branch's creator is not recorded by git at all and would need a walk of its own.

## Decision

A branch's authors are the authors of the commits it reaches that the trunk does not.

- The trunk is the default branch of [0589](0589-default-a-review-s-base-to-the-branch-tracking-origin-s-head.md) and its upstream, both hidden from the walk.
- A branch with no such commit counts its tip's author.
- With no trunk at all, every branch counts its tip's author.
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

- "Only mine" finds a branch a colleague also touched.
- One walk answers every branch.

### Negative

- A merged branch is attributed to whoever made its last commit.
- A trunk not named by `origin/HEAD`, and a detached HEAD, fall back to tip authors.
- `MaxCommitsPerTip` bounds each tip's answer but not the walk: a branch with a long history unrelated to the trunk is read whole, in the background, on every tip move.
- The listing is only fast because it reads HEAD and the branch configuration once per listing. `git_branch_is_head` re-reads HEAD and `git_branch_upstream` takes a new configuration snapshot on every call, which cost about 210 ms for 2001 branches; the listing now resolves HEAD once, compares reference names, and asks libgit2 for an upstream only for a branch whose `remote` and `merge` entries are both set.

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