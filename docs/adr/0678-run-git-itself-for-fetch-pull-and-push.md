# 0678. Run git itself for fetch, pull and push

Status: proposed
Date: 2026-11-08

## Context

The Git window gets Fetch, Pull and Push (spec `docs/superpowers/specs/2026-11-08-git-network-design.md`, D1-D8). The
vendored libgit2 has no SSH transport, which most remotes use, and no credential helpers: Git Credential Manager, the
default on Windows, works only through `git.exe`. The command line also brings proxies, `insteadOf` rewrites, hooks
(`pre-push`) and LFS, which a developer expects from a push.

[0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) rejected driving `git.exe` because another
program's text output would become a parsing contract and because git would become a hard runtime dependency.

The forces:

- **No one to answer.** The child runs without a console a user can see. A credential, passphrase or host-key prompt
  nobody can see is a hang.
- **A running child must be stoppable**, and the exit must never wait on one.
- **The user's own setup is theirs.** An inherited `GIT_SSH` or `GIT_SSH_COMMAND`, or a configured `core.sshCommand`,
  is how this user reaches their remotes.
- **Tests never touch a network.** Remotes are local bare repositories named by path.

## Decision

Run `git.exe` for fetch, pull and push, and for nothing else. Every read and every other write stays on libgit2.

**The contract with git is narrow.**
- The exit code decides success.
- stderr is shown verbatim: the toast carries git's last six lines, and the whole of stderr is what Copy puts on the
  clipboard.
- Only progress percentages and two failure hints (a rejected push; a failed sign-in) are read from it.
- The hint sentences are composed in Graphite, where the remote and the branch are known; Core reports only the hint
  kind and git's words.

**Finding git.** `PATH`, then the install root Git for Windows' installer recorded, then
`%ProgramFiles%\Git\cmd\git.exe`. There is no setting for a custom path. Without a git the three buttons are disabled
and say why; everything else works. An install outside those places is not found.

**Environment of every child.**
- `GIT_TERMINAL_PROMPT=0`, so git never prompts on the hidden console.
- `LC_ALL=C`, so messages are English and stable.
- When neither `GIT_SSH_COMMAND` nor `core.sshCommand` is set, `GIT_SSH_COMMAND=ssh -o BatchMode=yes`, so ssh fails
  instead of asking.
- An inherited `GIT_SSH` or `GIT_SSH_COMMAND`, or a configured `core.sshCommand`, is kept as it is. PuTTY's `plink`
  without `-batch` may then prompt on the hidden console, and the operation sits until the user cancels it.

**The commands.**
- Fetch: `git fetch --all --prune --progress`, one process for every remote. `--prune` is a call: the window shows a
  branch as deleted on its remote only if the fetch removed the remote-tracking ref, and the cost is that a fetch
  deletes remote-tracking refs the user might have wanted to look at. Tags follow git's default (tags that point into
  the fetched history); a tag git would not fetch is not fetched. A remote's default branch is recorded afterwards, for
  each remote that has no `refs/remotes/<remote>/HEAD`, with `git remote set-head <remote> --auto`; a change of the
  server's default is not tracked, so after a rename on the server the old default stays until the user runs that
  command.
- Fetch full history: the "History cut off here" banner of a shallow clone gains **Fetch full history**, which runs
  `git fetch --unshallow --progress` as a network operation with the same cancel and progress. Afterwards the graph
  re-reads, as it already does when the shallow boundary moves. The cost is that the whole history comes down at once,
  which on a large repository is a long, cancellable run.
- Pull is two commands, `git fetch <remote> --progress` then `git merge --ff-only <upstream>`, never `git pull`: the
  user's `pull.rebase` and `pull.ff` would decide what it did, and the window cannot yet show a rebase or a merge in
  progress. A branch that cannot fast-forward is refused with nothing changed.
- Push: `git push --progress <remote> HEAD:<upstream branch>` when the branch has an upstream. Without one the choice
  depends on the remotes: with one, `git push --progress --set-upstream <remote> HEAD`; with several, the button opens a
  menu, "Push <branch> to...", listing them by name in config order, because no remote is special, "origin" included;
  with none, Push is disabled and says why. Never forced, never with tags. The cost is one more click for a branch
  without an upstream in a repository with several remotes, and no way yet to force a push or send tags.
- `--` goes before every remote name, so a remote named like an option is not read as one.

**Running and stopping.**
- The operations return `NetworkOutcome`: `Done`, `Cancelled` or `Failed`, a failure carrying the text to show, the text
  to copy and the hint kind. A cancel is never reported as a failure.
- Cancel is a flag, polled every 50 ms by the code that waits on git.
- Only a cancel ends git's job object. A run that ends on its own leaves whatever git started running: a sign-in
  browser the credential manager opened must survive a successful fetch. A cancel can therefore end a browser opened
  during that run.
- A Graphite crash mid-run leaves git to finish by itself. Prompts are off and ssh is in batch mode, so git ends, and a
  fetch or push finishing is what the user asked for.
- The fast-forward step of Pull is not cancellable: a half-updated tree is worse than a wait. Cancel is disabled, and
  the button reads `Pulling...`, once the merge starts.
- Cancel ends the child and every process it started, which a job object does; Cancel is the running operation's own
  button, which reads `Fetching... 45%`, `Pulling...` or `Pushing...` while it runs, and the other two buttons are
  disabled.
- Shutdown and a change of folder cancel the running operation, so the queue never blocks the exit. The cost is that a
  fetch in flight when the user switches folders is lost and has to be started again.
- Success is quiet: the status bar shows the done text for a few seconds ("Fetched", "Pulled 5 commits", "Pushed 2
  commits") and there is no success toast. The cost is that a success is easy to miss; every failure is an error toast,
  with the whole of stderr to copy.
- Network operations run on their own one-job queue. Only Pull's fast-forward joins the write queue, so it never
  overlaps a commit, checkout or stash.

**In the code.** Core owns the runner and the operations (`Core/src/Core/Git/Command`). `GitPort` finds git
(`QueryNetworkSetup`: the git, the work tree, the common git dir, and whether `core.sshCommand` is configured, read
through libgit2), forwards the six operations and answers the time of the last fetch (`QueryLastFetch`: the mtime of
`FETCH_HEAD` in the common git dir, which a linked worktree's own git dir does not have).

## Rationale

Only the command line can reach most remotes the way the user already does. Keeping libgit2 for everything that reads
or writes the repository keeps the parsing contract to an exit code, stderr shown as it is, and two hints, which is
what 0491 objected to. The dependency on git is real but limited to three buttons.

## Consequences

### Positive

- Every remote the user's terminal reaches works: SSH, credential managers, proxies, rewrites, hooks, LFS.
- The text git prints is shown, so the user sees the real reason for a failure.
- The hang on an invisible prompt is closed for git's own prompts and for ssh.

### Negative

- Fetch, Pull and Push need git installed in one of three places; no custom path in this stage.
- A user's `plink` without `-batch` may prompt invisibly until Cancel.
- A straggler git, or a credential-manager window, can outlive a run or a Graphite crash.
- A cancel can end a sign-in browser opened during that run.
- The cancel flag is polled every 50 ms, so a cancel takes up to that long.
- Pull's last step cannot be stopped.
- Git Credential Manager still shows its own window, which is a separate program.
- Pull cannot merge or rebase for now: a diverged branch is a terminal step.
- The server's renamed default branch is not followed, since that needs `remote.<r>.followRemoteHEAD=always`, which would
  overwrite a head the user set.

### Neutral

- No fetch on a timer; the user fetches by hand.
- Tests run git against local bare repositories by path, with git's system and global configuration emptied, and skip,
  saying why, when git is missing.

## Alternatives Considered

- **libgit2 for the network.** Rejected: no SSH, no credential helpers, none of the user's transport setup.
- **`git pull`.** Rejected: the user's configuration would pick a rebase or a merge the window cannot show.
- **An in-app credential prompt (an askpass helper).** Deferred, not rejected: it is a feature of its own, and the
  credential manager's window covers the common case.
- **Killing git when its run ends.** Rejected: it would close a sign-in browser the credential manager opened for a
  fetch that succeeded.

## Related

- [0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) - the rejection this narrows.
- [0492](0492-give-each-worker-thread-its-own-repository-handle.md) - the worker threads the port runs on.
- `docs/superpowers/specs/2026-11-08-git-network-design.md` and
  `docs/superpowers/plans/2026-11-08-git-window-stage5-network.md` - the design and the plan this records.
