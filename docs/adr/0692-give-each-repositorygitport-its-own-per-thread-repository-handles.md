# 0692. Give each RepositoryGitPort its own per-thread repository handles

Status: proposed
Date: 2026-11-09

## Context

A libgit2 handle may be used by one thread at a time, so `RepositoryGitPort` keeps one handle per worker thread per
root in `thread_local` maps ([0492](0492-give-each-worker-thread-its-own-repository-handle.md)), and drops a handle of
any other root on that thread's next call. Until the worktree preview those maps were shared by every port on a thread,
which [0681](0681-keep-repositorygitport-s-forwarding-bodies-thread-local-handles-and-one-gitport.md) kept because the
application owned one port, noting that a second port would make the shared maps the first thing to change.

The worktree preview added that second port: the Git window reads the worktree it looks at through a port of its own,
pointed at that worktree, on the same job queue as the open folder's port. With one map per thread, each query of one
port found the other's root in the map, dropped that handle and opened its own, and the next query of the other port
did the same. Every query reopened a repository, and every reopen threw away the commits libgit2 had parsed into the
handle, so history and line reads started cold each time.

## Decision

- **Each port's handles are its own.** The per-thread maps are keyed first by the port's number and then by root. A
  port takes its number from a process-wide counter when it is constructed; numbers are never reused, so a port made
  where another was destroyed never takes over its handles. The content checks' in-memory handles are kept apart in the
  same way.
- **Letting go reaches one port.** `LetGoOfHandlesNoLongerCurrent` drops the calling thread's handles of this port that
  are of another root or an older generation, and touches no other port's. Re-pointing the worktree port
  (`SetRoot`) submits it to one job worker and to the write worker; the other job workers let go on their next query of
  that port.
- **Lifetime.** A port outlives every queue whose jobs use it: its owner declares the port before the models and the
  models before the queues, so the queues are destroyed first and join their workers. Each worker thread frees its
  `thread_local` maps, every port's handles in them, as it exits. A port's destructor erases only the destroying
  thread's entry for its number; no other thread can be reached from there, and by then none is left that used it.

## Rationale

Keying by port keeps 0492's rule — one handle per thread per repository, never shared across threads — and removes the
interference without moving the maps out of `thread_local` storage, which 0681 kept for good reason: a map owned by the
instance would have to be emptied from the UI thread while workers read it. The lifetime rule is the one the owners
already follow for models and queues, so it adds no new ordering to get wrong.

## Consequences

### Positive

- The open folder's port and the worktree port keep their handles and their parsed commits across each other's
  queries.
- A port's handle count is measurable per port (`GetLiveHandleCountForThisThread`), which the tests use.

### Negative

- A job worker that does not query the worktree port again keeps a handle on the worktree looked at before, with its
  packfiles mapped, until it queries the port again or exits.
- Each worker can now hold one handle per port per repository, two where it held one.
- The lifetime rule is kept by declaration order and comments; declaring a queue before a port it serves compiles and
  fails only under real threading.

### Neutral

- A port that only ever runs on one thread behaves as before.

## Alternatives Considered

- **One shared map per thread, as before.** Correct answers, and a reopen of the repository on every alternating query,
  losing every parsed commit.
- **Maps owned by the port instance.** The port could not empty a worker's map from the UI thread while that worker
  reads, so each worker would still need its own map inside the instance, keyed by thread, with a lock — the same
  structure with a lock added.
- **One port for both folders, re-rooted per query.** Every switch is a generation bump that drops the handle, the same
  churn as the shared map.

## Related

- [0492](0492-give-each-worker-thread-its-own-repository-handle.md): the per-thread rule these handles follow.
- [0681](0681-keep-repositorygitport-s-forwarding-bodies-thread-local-handles-and-one-gitport.md): kept the handles
  `thread_local` with one port; this record is the change it named for a second port.
- [0689](0689-list-every-worktree-in-the-git-window-s-sidebar-ordered-by-activity-and-fixed-between-rebuilds.md): the
  worktree looked at, which the second port reads.
- `docs/superpowers/specs/2026-11-08-worktree-preview-design.md` §3 (reads of another worktree), §10 (how the second
  changes model is built).
- `Graphite/src/Git/RepositoryGitPort.cpp` (`HandleMapsByPort`, `NextPortNumber`), `Graphite/src/App/GraphiteLayer.h`
  (the declaration order of `m_GitPort`, `m_WorktreeGitPort` and the queues).
