# 0681. Keep RepositoryGitPort's forwarding bodies, thread-local handles and one GitPort

Status: rejected
Date: 2026-11-08

## Context

The architecture review of 2026-11-08 found `RepositoryGitPort` (1,596 lines,
seven ports) mostly forwarding: about thirty methods open this thread's handle,
return its error, or call one `Core::Repository` method. Its handles live in
`thread_local` maps keyed by root text, shared by every instance on a thread, and
`LetGoOfHandlesNoLongerCurrent` has to run on each worker. `GitPort` has 42 pure
virtuals, nine of them for the network. Three changes were proposed: one helper
that runs a callable with the handle, handle maps owned by the instance, and a
`NetworkPort` split out of `GitPort` for `NetworkModel`.

## Decision

Make none of the three changes.

- The forwarding bodies stay. The helper would be a template whose only purpose
  is to accept a callable, which
  [0563](0563-write-concrete-code-and-keep-templates-for-vocabulary-types.md)
  rules out, and a `std::function` cannot carry the thirty different result
  types.
- The handles stay `thread_local`. A libgit2 handle may be used by one thread at
  a time, so each worker keeps its own whoever owns the map, and the map cannot
  be emptied from the UI thread while a worker reads. Two instances on one thread
  cost each other reopened handles, not wrong answers, and the application owns
  one.
- `GitPort` stays whole. `NetworkModel` takes a `GitModel`, which takes a full
  `GitPort`, so a test of the network model needs a whole port anyway; and
  `GitModel` itself asks `QueryLastFetch`, `QueryNetworkSetup` and
  `FastForward`. The test doubles already keep the network half in one place,
  `NetworkDoublePort` under `EmptyGitPort`.

## Rationale

Each change passes the code-size test and fails the deletion test: nothing a
caller or a test does gets simpler, and the thread rule they would hide is one
libgit2 imposes.

## Consequences

### Positive

- The port keeps one way to reach a handle, readable without templates.

### Negative

- A new port method still costs a thirteen-line forwarding body.
- The thread rule is still kept by comments and by `FolderParticipants` calling
  `LetGoOfHandlesNoLongerCurrent` on each worker.

### Neutral

- If the application ever holds two ports, the shared maps become the first
  thing to change.

## Alternatives Considered

- **A `Result<Repository&>` accessor in place of the pointer.** Saves one line
  per body and changes nothing a reader has to know.

## Related

- [0492](0492-give-each-worker-thread-its-own-repository-handle.md) is the per-thread rule
  these handles follow.
- [0563](0563-write-concrete-code-and-keep-templates-for-vocabulary-types.md)
  is why the forwarding is not collapsed by a template.
