# 0690. Write into another worktree only when it is as the user saw it

Status: proposed
Date: 2026-11-09

## Context

The Git window shows another worktree's uncommitted changes and lets the user stage, unstage and commit there without
leaving the open folder ([0689](0689-list-every-worktree-in-the-git-window-s-sidebar-ordered-by-activity-and-fixed-between-rebuilds.md)).
That worktree usually belongs to someone else, most often an agent that stages, commits and switches branches in it at
any moment. Between the user's look and the click, the branch may have changed, a commit may have landed, or more files
may have been staged. A write that runs anyway records work the user never saw, or lands on a branch the window did not
show, which are the two outcomes the feature must never have.

The open folder's writes already go through one write worker (ADRs 0500, 0636) and are reported in the status bar
(ADR 0635). Another worktree's index is rewritten by its own git, so nothing cached about it can be trusted, and while
an agent's git is writing, the worktree's `index.lock` is held.

## Decision

- **A write carries what the user saw.** Every stage, unstage and commit asked from another worktree's changes view
  carries a `WriteExpectation`: the HEAD kind, the branch, the HEAD commit, the worktree's folder and its name, and for
  Commit and Amend the sorted set of staged paths. It is built from the state the view drew when the user clicked: an
  Amend of a pushed commit carries what was seen at the click into its question, not what is there once the question
  is answered. Commit and Amend wait until the worktree's HEAD has been read, since before that the user has seen no
  branch to commit to.
- **The worktree's port reads that folder and no other.** It opens the repository exactly at the worktree's folder
  (`RootReach::Exactly`, ADR 0692), never searching above it: a worktree folder that lost its `.git` file, as when
  `git worktree remove` fails partway on a locked file, reads as no repository instead of as the checkout around it,
  where a stage or commit would otherwise land.
- **The write worker checks it just before the write**, on the same worker and with the same port the write uses,
  reading HEAD and, for a commit, the status with a fresh index. The write is refused, with nothing changed and the
  lists read again, when:
  - the port opens another working directory than the worktree's folder (`Not staged: <folder> no longer opens as that
    worktree`) — every write, checked before HEAD, whose answer would be the other checkout's;
  - HEAD is on another branch, attached or detached differently than shown, or, for a detached HEAD, at another
    commit than shown (a detached HEAD has no branch to commit on top of) — every write;
  - HEAD has moved and the write is an Amend — it would rewrite a commit the user has not seen;
  - the staged set differs and the write is a Commit or an Amend — it would record files the user did not see staged.

  A Commit when HEAD moved on the same branch with the staged set unchanged goes ahead, on top of the new commit, which
  the history already shows. Stage and unstage of single files check only the head.
- **The refusal names the folder and what changed**: `Not committed: <folder> got a new commit since you looked`,
  `Not staged: <folder> has another branch or a detached HEAD checked out since you looked`, `Not committed: the staged
  files in <folder> changed since you looked`.
- **A held index lock is "busy", not a failure to explain.** A write whose failure is libgit2's "the index is locked"
  reads `Git is busy in <folder>; try again`. Nothing retries on its own. The match is on libgit2's sentence, which
  names no file, so a user file called `index.lock` is never mistaken for it.
- **The view says where a commit goes.** Above the lists: `Worktree · <full path>`, with a merge or rebase in progress
  named on the same line. The button reads `Commit to <branch> in <folder>`, or `Commit on detached HEAD` in the warning
  colour; the empty message box reads `Message for <branch> in <folder>` or `Message for detached HEAD in <folder>`. A
  label too long for the button is cut, with the whole text in its tooltip.
- **The view offers only what records work.** No Discard, no Stash, no Revert of a file, and no Initialize for a
  worktree whose repository is gone (it says the folder is missing instead). Amend of a commit already pushed asks
  first, as in the open folder (ADR 0686), with the branch and its upstream read from the open folder's branch list,
  which every worktree of the repository shares.
- **Each worktree has its own commit draft** (message and Amend), kept for the session and never shown in the side
  panel; the open folder's draft stays shared as ADR 0656 says.
- Writes into another worktree run on the same write worker as the open folder's, one at a time, and are reported in
  the status bar naming the folder. ADR 0554 applies as in the open folder.
- **A commit or an amend that lands tells the open folder.** It moves a branch every worktree of the repository shares,
  and the workspace's watcher never sees `.git/refs` when the open folder is a subfolder of the repository or a linked
  worktree itself, so the application publishes `RepositoryChangedEvent` as it does after a fetch.

## Rationale

Checking on the worker that runs the write, rather than on the UI thread when the button is clicked, closes the gap in
which an agent's change could slip in unseen, as `FastForwardRequest` already does for a fast-forward. Refusing names
the reason and costs one more click; committing unseen work into someone else's branch costs a history rewrite. The one
change let through, a plain Commit on top of a new commit on the same branch, records exactly the files the user saw on
the branch the user saw.

## Consequences

### Positive

- A commit, stage or unstage never lands on a branch the window did not show, and a commit never records a staged file
  the user did not see.
- An agent's git at work reads as busy, with the folder named, rather than as an opaque libgit2 error.
- Nothing in this view can destroy work in progress.

### Negative

- A small window remains between the check and the write on the worker; an agent that stages in it is recorded. Closing
  it would need holding the worktree's index lock across the check and the write.
- A detached worktree whose HEAD moved refuses every write, even a stage that would be harmless; the user must look
  again first.
- Discarding or stashing in another worktree needs that folder opened, or a terminal.

### Neutral

- The open folder's own writes carry no expectation and behave as before.
- The refusal reads the lists again, so the next look is already the current one.

## Alternatives Considered

- **Check on the UI thread when the button is clicked.** Simpler, and leaves the whole queue wait as a gap in which the
  worktree can change unseen.
- **Lock the worktree while its changes are shown.** Would block the agent working there, which is the work being
  looked at.
- **Retry a busy index automatically.** Hides that another process is writing, and a retry after the agent's write is
  a write into a state the user has not seen.
- **Offer Discard and Stash as in the open folder.** One misclick away from destroying an agent's work in progress,
  for a view whose purpose is looking and recording.

## Related

- [0689](0689-list-every-worktree-in-the-git-window-s-sidebar-ordered-by-activity-and-fixed-between-rebuilds.md):
  the section and the changes row the writes are asked from.
- [0500](0500-serialise-git-writes-in-one-queue.md), [0636](0636-run-git-writes-on-a-worker-of-their-own.md): the
  write worker.
- [0692](0692-give-each-repositorygitport-its-own-per-thread-repository-handles.md): the worktree's own port, which
  opens exactly at its folder.
- [0635](0635-report-each-git-write-in-the-status-bar-from-the-click-until-the-status-after-it-lands.md): the status
  bar report.
- [0554](0554-refuse-a-commit-that-records-head-s-tree-unless-it-amends-or-concludes-a-merge.md),
  [0656](0656-draw-changes-as-one-view-hosted-by-the-side-panel-and-the-git-window-sharing-one-commit-draft.md),
  [0686](0686-force-push-only-with-a-lease-only-to-the-branch-s-own-upstream-and-only-after-a-question.md).
- `docs/superpowers/specs/2026-11-08-worktree-preview-design.md` §6 (the changes details, the draft, what is not
  offered, writes and their outcome), §9 (merge or rebase in another worktree).
- `Graphite/src/Git/Worktrees/WriteExpectation.h`, `Graphite/src/Git/GitModelWrites.cpp`,
  `Graphite/src/Git/Changes/ChangesView.h` (`ChangesViewOptions`).
