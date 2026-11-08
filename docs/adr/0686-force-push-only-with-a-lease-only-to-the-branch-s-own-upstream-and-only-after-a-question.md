# 0686. Force push only with a lease, only to the branch's own upstream, and only after a question

Status: proposed
Date: 2026-11-08

## Context

Push never forced (ADR 0678's network operations). The Changes view offers
Amend, and amending a commit that is already pushed leaves the branch one
ahead and one behind its upstream. A hands-on test pass on 2026-11-08 did
exactly that: Push was refused, Pull refused a merge, and nothing in the
application could put the branch right — the reader had to know to open a
terminal and type a force push.

## Decision

Offer **Git: Force Push...** in the Git menu, enabled only while Push is
and the checked-out branch has diverged from its upstream (each has commits
the other lacks). It asks first, naming the upstream, the branch and how
many of the upstream's commits are dropped for everyone, and runs
`git push --force-with-lease=<branch> -- <remote> HEAD:<branch>` only to the
branch's own upstream of the same name; anything else fails without running
git. The lease is the remote-tracking branch as the last fetch left it, so
a commit someone else pushed since is never thrown away: git refuses with
"stale info", which reads as a rejected push and says to pull first.

Before an Amend is committed, the Changes view asks when the branch's
upstream already holds the last commit (the branch is not ahead of it),
saying that the branch and its upstream will part ways and that Force Push
is then what replaces the commit on the remote.

`PushTo` itself still never forces.

## Rationale

The lease is what makes a force push from a button acceptable: the only
commits it can drop are ones the reader has fetched and so has seen in the
history. Restricting it to the branch's own upstream keeps it from ever
moving a branch the reader did not push from here. Asking before the amend,
not only before the force push, catches the mistake while undoing it is
still free.

## Consequences

### Positive

- An amended pushed commit no longer strands the reader without a terminal.
- A colleague's commit pushed after the last fetch survives a force push.

### Negative

- A force push still drops the fetched commits on the remote for everyone;
  the question is the only guard against dropping them on purpose by
  mistake.
- The menu item stays disabled for a branch that diverged from an upstream
  of another name, where a terminal is still needed.

### Neutral

- Merge and rebase of a diverged branch remain outside the application.

## Alternatives Considered

### Only warn before amending

Rejected: it prevents the dead end but leaves anyone who amended anyway, or
in a terminal, with no way out in the application.

### Plain --force

Rejected: it drops whatever is on the remote, including commits nobody here
has seen.

## Related

- ADR 0678 — the network operations this adds to.
- `Core/src/Core/Git/Command/GitNetwork.h` — `ForcePushWithLease`, and
  `HintOf` reading "stale info" as rejected.
- `Graphite/src/Git/Network/NetworkModel.h` — `ForcePush`.
- `Graphite/src/App/GraphiteLayer.cpp` — `DivergedBranch` and
  `RenderForcePushConfirm`.
- `Graphite/src/Git/Changes/ChangesView.cpp` — `PushedLastCommit` and
  `RenderAmendPushedConfirm`.
