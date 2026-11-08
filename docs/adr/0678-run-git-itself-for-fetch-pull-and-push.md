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
- Whether `core.sshCommand` is set is asked of git itself before each operation, with `git config --get
  core.sshCommand` in the folder: exit 0 is set, even to nothing; 1 is unset; anything else, a run that cannot start
  included, counts as set. libgit2 cannot follow every include git does: the vendored 1.8.4 knows no `includeIf
  "hasconfig:remote.*.url:…"`, a documented way to give one organisation's remotes their own key, and a command it
  missed would be replaced by the batch-mode one. The cost is one short run of git before each operation's.
- `GIT_DIR`, `GIT_WORK_TREE`, `GIT_INDEX_FILE`, `GIT_COMMON_DIR`, `GIT_OBJECT_DIRECTORY`,
  `GIT_ALTERNATE_OBJECT_DIRECTORIES`, `GIT_NAMESPACE` and `GIT_PREFIX` are taken out of every child's environment
  (`ChildProcess` removes a variable as well as setting one). Graphite started from a git hook or alias inherits them,
  and git would then act on another repository than the one libgit2 opened for the window. `GIT_CONFIG_PARAMETERS`,
  `GIT_SSH`, `GIT_SSH_COMMAND` and the rest are the user's own and kept.

**Restricted mode.** git runs the folder's hooks (`pre-push`, `post-merge`, `reference-transaction`) and the commands
its config names (`core.sshCommand`, `core.fsmonitor`, a `credential.helper` that starts with `!`,
`remote.<remote>.uploadpack` and `receivepack`), which a folder's own `.git` controls. In a folder that is not trusted
(`WorkspaceTrust::IsTrusted` is false for the open folder) the three buttons, the three Git menu commands and the
history banner's Fetch full history are disabled, each saying "Fetch, Pull and Push run git, which runs this folder's
hooks and the commands its git config names. Trust the folder to use them." The window asks the trust each frame, as
the language server is asked through its root permission, so trusting the folder enables them at once; an operation
already running when the trust is taken back can still be cancelled. The menu shows no reason, since a command's note is
fixed. This amends [0532](0532-open-an-unknown-folder-in-restricted-mode-until-the-user-trusts-it.md)'s "Git works as
before" for these network operations only: every read and write through libgit2 works in restricted mode as before. The
cost is that an untrusted folder fetches, pulls and pushes only from a terminal.

**The commands.**
- Fetch: `git fetch --all --prune --progress`, one process for every remote. `--prune` is a call: the window shows a
  branch as deleted on its remote only if the fetch removed the remote-tracking ref, and the cost is that a fetch
  deletes remote-tracking refs the user might have wanted to look at. Tags follow git's default (tags that point into
  the fetched history); a tag git would not fetch is not fetched. A remote's default branch is recorded afterwards, for
  each remote that has no `refs/remotes/<remote>/HEAD`, with `git remote set-head <remote> --auto`; a change of the
  server's default is not tracked, so after a rename on the server the old default stays until the user runs that
  command.
- Fetch full history: the "History cut off here" banner of a shallow clone gains **Fetch full history**, which runs
  `git fetch --unshallow --progress` as the Fetch button's operation, since it is a fetch: while it runs the Fetch
  button shows the busy mark and the percent, its tooltip reads `Fetching full history… 45%` and "Click to cancel", and
  a click on it cancels. The banner's action is disabled for Fetch's own reasons (no git, no remote, a folder not
  trusted) and while any network operation runs, its tooltip saying which. Afterwards the history
  and the lines are read again, whoever deepened the clone, a terminal included, all the way or part way: libgit2 keeps
  the commits it parsed with their parents cut, so the port opens the repository afresh once the shallow file changes;
  the branch listing is then a new snapshot even though no ref moved; and the line reader starts its graph afresh once
  a commit it holds cut is no longer on the shallow boundary. The cost is that the whole history comes down at once, which on a
  large repository is a long, cancellable run.
- Pull is two commands, `git fetch <remote> --progress` then `git merge --ff-only --no-autostash <upstream>`, never
  `git pull`: the user's `pull.rebase` and `pull.ff` would decide what it did, and the window cannot yet show a rebase
  or a merge in progress. A branch that cannot fast-forward is refused with nothing changed. `--no-autostash` keeps the
  user's `merge.autoStash` from stashing local changes and laying them back over the merge as a conflict while Pull
  reports success: the merge refuses, and the local-changes toast names the files. It needs git 2.27 or later, and an
  older git fails Pull with its own words. A branch whose upstream was deleted on its remote, as `fetch --prune` leaves
  one after its pull request is merged, is not pulled, saying `feature's upstream origin/feature no longer exists.`
- Push: `git push --progress --no-follow-tags -- <remote> HEAD:<upstream branch>` when the branch has an upstream on a
  remote and both have one name, compared exactly, as git's own default push compares them. A branch whose upstream has
  another name, as `git checkout -b my-fix origin/main` sets one up, is published under its own name instead, with the
  command of a branch without an upstream, and tracks that from then on; its tooltip says so, `Push my-fix to origin as
  origin/my-fix; it then tracks origin/my-fix instead of origin/main`. This amends spec D4, which sent HEAD to the
  upstream whatever its name: Push on my-fix would have moved the server's main to the branch's commits, which only a
  force push takes back. The cost is that a branch made from origin/main tracks origin/<its name> after its first push.
  A branch whose upstream is a local branch keeps it: HEAD goes to the remote chosen, under the branch's name, without
  `--set-upstream`. Without an upstream the choice depends on the remotes: with one, `git push --progress
  --no-follow-tags --set-upstream -- <remote> HEAD`; with several, the button opens a menu, "Push <branch> to...",
  listing them by name in the order the sidebar lists them, which is the names' order, because no remote is special,
  "origin" included, and no name is read for meaning; with none, Push is disabled and says why. A branch whose upstream
  was deleted is pushed as one without an upstream, which makes the remote branch anew, and with one remote its tooltip
  names it, `Push feature to origin as origin/feature`. Fetch's tooltip names the remotes in the same order. Never
  forced, never with tags: `--no-follow-tags` keeps the user's `push.followTags` from sending the annotated tags on the
  commits pushed. The cost is one more click for a branch without an upstream in a repository with several remotes, a
  remote added first that is not necessarily listed first, and no way yet to force a push or send tags.
- `--` goes before every remote name, so a remote named like an option is not read as one.

**Running and stopping.**
- The operations return `NetworkOutcome`: `Done`, `Cancelled` or `Failed`, a failure carrying the text to show, the text
  to copy and the hint kind. A cancel is never reported as a failure.
- Cancel is a flag, polled every 50 ms by the code that waits on git.
- A cancelled fetch can leave the pack it was receiving in `objects/pack` as `tmp_pack_*`: in the acceptance, 6 to 87
  MiB of a 512 MiB fetch, still there after the next fetch. The app never deletes one, since a git run in a terminal
  may be writing a `tmp_pack_*` of its own beside it; `git gc` removes them once they are older than its prune expiry,
  two weeks by default. The cost is that disk space until then.
- Only a cancel ends git's job object. A run that ends on its own leaves whatever git started running: a sign-in
  browser the credential manager opened must survive a successful fetch. A cancel can therefore end a browser opened
  during that run.
- A Graphite crash mid-run leaves git to finish by itself. Prompts are off and ssh is in batch mode, so git ends, and a
  fetch or push finishing is what the user asked for.
- The fast-forward step of Pull is not cancellable: a half-updated tree is worse than a wait. Once the merge starts the
  Pull button is disabled, still running, and its tooltip reads `Pulling… the last step cannot be cancelled`. Exit and
  the write queue wait for the merge's end, since it runs on the write queue, which neither a change of folder nor
  shutdown stops. With Git LFS, `submodule.recurse` or a slow `post-merge` hook that wait can include their downloads.
  The app shows no wait at exit: its window hides at once, and the process ends once the merge has. The cost is an exit
  held for the merge's end, and every commit, checkout and stash waiting behind it meanwhile.
- Cancel ends the child and every process it started, which a job object does; Cancel is the running operation's own
  button, Fetch's for Fetch full history. While it runs, its icon becomes the busy mark, its count slot shows the
  percent, and its tooltip says `Fetching… 45%`, `Fetching full history… 45%`, `Pulling…` or `Pushing…` and "Click to
  cancel". The other two buttons are disabled and keep their counts, dim: the running state changes only its own button.
- The percent is git's own. `git fetch --all` with several remotes starts one fetch per remote and passes `--progress`
  to none of them, so they print no progress: such a Fetch shows the busy mark without a percent. Fetching remote by
  remote would show one, at the cost of the single process this decision keeps for every remote. With one remote, and
  for Pull's fetch, Push and Fetch full history, the percent is shown.
- Shutdown and a change of folder cancel the running operation, Fetch full history included, so the network queue never
  blocks the exit; only Pull's fast-forward, on the write queue, is waited for. The cost is that a fetch in flight when
  the user switches folders is lost and has to be started again.
- Success is quiet: the status bar shows the done text for a few seconds ("Fetched", "Pulled 5 commits", "Pushed 2
  commits") and there is no success toast. The cost is that a success is easy to miss; every failure is an error toast,
  with the whole of stderr to copy.
- Network operations run on their own one-job queue. Only Pull's fast-forward joins the write queue, so it never
  overlaps a commit, checkout or stash.

**In the code.** Core owns the runner and the operations (`Core/src/Core/Git/Command`). `GitPort` finds git
(`QueryNetworkSetup`: the git, the work tree and the common git dir), forwards the six operations, each after
`Core::IsSshCommandConfigured` has asked git about `core.sshCommand`, and answers the time of the last fetch
(`QueryLastFetch`: the mtime of `FETCH_HEAD` in the common git dir, which a linked worktree's own git dir does not
have). The age shown is that of the last fetch that ended well: git writes `FETCH_HEAD` before it reaches the remote,
so after a fetch of the window's (Fetch, Pull's fetch, Fetch full history) fails or is cancelled, the age from before it
stays shown until `FETCH_HEAD`'s time changes again. The cost is that a fetch that failed in a terminal still reads as
a fetch, since nothing says how it ended.

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
- A cancelled fetch can leave a partial pack file behind until `git gc` removes it.
- Pull's last step cannot be stopped, and exit waits for it, downloads that LFS, submodules or hooks make included,
  with no wait shown.
- An untrusted folder cannot fetch, pull or push from the window.
- A branch made from an upstream of another name tracks a branch of its own name after its first push.
- Each operation runs git once more, to ask whether `core.sshCommand` is set.
- Pull needs git 2.27 or later.
- A Fetch of several remotes shows no percent, only the busy mark.
- The push menu lists the remotes by name, not in the order they were added.
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
- [0532](0532-open-an-unknown-folder-in-restricted-mode-until-the-user-trusts-it.md) - restricted mode, whose "Git works
  as before" this amends for fetch, pull and push.
- `docs/superpowers/specs/2026-11-08-git-network-design.md` and
  `docs/superpowers/plans/2026-11-08-git-window-stage5-network.md` - the design and the plan this records.
