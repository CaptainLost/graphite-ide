# 0589. Default a review's base to the branch tracking origin's HEAD

Status: proposed
Date: 2026-10-14

## Context

[0580](0580-review-a-branch-against-its-merge-base-with-the-base-taken-from-head.md) took a review's default base from HEAD: any other branch was offered against the checked-out branch, and the checked-out branch against its upstream. That holds while the trunk is checked out. Once a feature branch is, every other branch is offered against it, the trunk included. The trunk against a branch that already contains it is an empty review, and the trunk's own unpushed commits are never offered at all.

A clone records the remote's default branch as `refs/remotes/origin/HEAD`. Nothing else in the repository names the trunk.

## Decision

The default base is the default branch, whatever is checked out:

- The default branch is the local branch tracking the branch `origin/HEAD` names, the one of the same name first (`main` for `origin/main`). With no local branch tracking it, it is that remote branch.
- Every other branch, local or remote, the checked-out one included, is offered against the default branch.
- The default branch is offered against its upstream, and nothing without one.
- A repository without `origin/HEAD` keeps 0580's rule: the checked-out branch, its own upstream for itself, and a submenu of local branches while HEAD is detached.

Only `origin` is read. The HEAD of any other remote is not a default.

`Core::Branch::IsRemoteDefault` marks the remote branch its remote's HEAD names. It comes with the branch list, so the menu asks git nothing when it opens.

A session keeps the base it started with ([0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md)).

## Rationale

**The trunk does not move when the checkout does.** A review asks what a branch would bring into the trunk. The checked-out branch answers that only while it is the trunk.

**The local branch, not the remote one.** A branch forked from a local trunk with unpushed commits contains them. Against `origin/main` those commits would show as the branch's own work.

**HEAD is still the best guess without `origin/HEAD`.** A repository that does not name its trunk leaves the checked-out branch as the only choice the user has made. Guessing by name (`main`, then `master`) would add a rule that is wrong for a trunk named otherwise.

## Consequences

### Positive

- Reviews of the same branch get the same default base wherever the checkout sits.
- The trunk is offered its unpushed commits.
- Detached HEAD needs no pick when `origin/HEAD` exists.

### Negative

- `origin/HEAD` exists only after a clone. A remote added by hand and pushed to leaves the old rule in force until `git remote set-head origin --auto` is run, and nothing in Graphite says so.
- A repository whose trunk lives on a remote not named `origin` gets the old rule.
- Two rules now decide the default, and which one applies depends on a ref the user rarely sees.

### Neutral

- A branch already contained in the default branch is still offered, and its review opens as merged.

## Alternatives Considered

- **Keep HEAD and fall back to the upstream when the review would be empty.** Fixes the trunk's own menu, but every other branch still follows the checkout, so the same branch gets a different base after a checkout.
- **Always ask for the base.** Never wrong, but it costs a choice on every review for a value that is nearly always the trunk.
- **Guess the trunk by name.** Right in most repositories, wrong for one whose trunk is `develop` or `trunk`, and silent about it.

## Related

- [0580](0580-review-a-branch-against-its-merge-base-with-the-base-taken-from-head.md): its default-from-HEAD rule is replaced by this record; the rest stands.
- [0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md): the session that keeps the base.
- [0002](0002-keep-core-free-of-ide-knowledge.md): `IsRemoteDefault` names no review.
