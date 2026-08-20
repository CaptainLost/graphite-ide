# 0491. Keep git data in Core and git freshness in Graphite

Status: proposed
Date: 2026-08-20

## Context

Git support has two distinct concerns. One is answering questions about a
repository on disk — status, branches, log, diff, blame — which is data the
same way a file tree or a text buffer is data:
[ADR 0002](0002-keep-core-free-of-ide-knowledge.md) already places that kind
of thing in `Core`, where it can be tested headless. The other is deciding
*when* to ask those questions again — which events should trigger a refresh,
how long to wait for a burst of file changes to settle, how many commits to
keep in memory — and that is a product decision about how this particular IDE
behaves, not a fact about a git repository.

`Core::JobQueue` already exists and is the mechanism `Graphite` uses to run
work off the UI thread. Refresh policy needs exactly that queue, plus
`Core::EventBus` to hear about workspace changes, both of which are wired up
in `Graphite`'s frame loop already.

## Decision

The read model — `Oid`, `Signature`, `FileStatus`, `WorkingTreeStatus`,
`Branch`, `CommitSummary`, `Diff`, `Blame`, `GraphLayout`, and the `Query*`
members of `Core::Repository` that produce them — lives in `Core/src/Core/Git`.
Every one of those queries is synchronous, uncached, and returns
`Result<T>`; none of it knows about threads, debouncing, or the event bus.

Refresh policy — `GitModel` — lives in `Graphite/src/Git`. It holds the
snapshots, decides when a query is stale, subscribes to workspace events,
debounces a burst of file changes into one refresh, and submits the actual
querying to `Core::JobQueue`. `GitModel` does not include or reference ImGui,
so it is tested the same way the read model is: headless, in `GraphiteTests`,
without a window.

## Rationale

The boundary this record draws is the same one ADR 0002 already drew, applied
to a new subsystem: facts about a repository are Core; decisions about this
application's behaviour are Graphite. Putting refresh policy in Core would
mean `CoreTests` — which runs with no threads of its own — would have to
synchronise against a job queue to test a debounce timer that has nothing to
do with git as a concept. Putting the read model in Graphite would put it out
of reach of anything built on Core that is not this IDE, and out of
`CoreTests`'s headless coverage.

## Consequences

### Positive

- `Core::Repository::Query*` is tested in `CoreTests` against a real fixture
  repository, with no job queue, no event bus, and no frame loop involved.
- `GitModel`'s debounce and generation-ordering logic is tested in
  `GraphiteTests` against a fake port, with no libgit2 handle and no thread
  involved.
- Either half can be reused on its own: the read model by any future headless
  tool, the refresh policy shape by any other kind of background-refreshed
  state Graphite adds later.

### Negative

- A single feature — "the status bar shows a stale badge" — has its logic
  split across two projects, so understanding it fully means reading both
  `Core::Repository::QueryStatus` and `Graphite::GitModel::OnUpdate`.
- The two halves communicate through `GitPort`, an interface that exists
  purely to let `GitModel` be tested without libgit2 — one more file than a
  single-project design would need.

### Neutral

- `Workspace::m_Repository`, which already exists for `IsIgnored` on the UI
  thread, is left as it is; this record does not fold it into `GitModel`.

## Alternatives Considered

### A `Core::GitService` owning the cache and its own refresh loop

Keeps everything git-related in one project. Rejected because it moves
refresh policy — event subscriptions, debounce timing, generation counters —
into a library that is supposed to be usable without an application around
it, and because `CoreTests` would then need to synchronise threads to test
timing logic that has nothing to do with what a repository contains.

### Driving `git.exe` as a subprocess instead of libgit2

Avoids linking a git library at all. Rejected because it makes another
program's text output a parsing contract, and adds a hard runtime dependency
on `git` being present in `PATH` — a strictly worse position than the one
already held, where libgit2 is vendored and linked in.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — the boundary this record
  applies to git.
- [0492](0492-give-each-worker-thread-its-own-repository-handle.md) — how the
  Graphite half runs those Core queries off the UI thread.
- `docs/superpowers/specs/2026-08-20-git-ui-design.md` — the design this
  record was extracted from.
