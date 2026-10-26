# 0644. Read solutions off the UI thread

Status: proposed
Date: 2026-10-26

## Context

`Workspace` read the solution and every project file it names on the UI thread.
That happens when a folder opens, when a solution is chosen in the Explorer
header, when the watcher reports a change in the solution's folders, and on
Refresh.

Measured in Release, the first read of real solutions with 20–27 projects took
95–266 ms, and 3–10 ms once the files were in the OS cache. A solution of
hundreds of projects on a cold disk would freeze the window for seconds.

## Decision

The solution is read in a job.

- **Before the first answer**, the solution view shows the loader "Reading the
  solution".
- **During a later read**, the view keeps the tree it has and adds a loader line
  "Reading the solution again".
- **An edit made in the solution view** saves the file and then starts a read.
- **A result for a closed folder, or for a read that a newer one replaced**, is
  dropped.

## Rationale

Reading many project files is disk work whose cost the user's solution decides.
The view already has a natural waiting state.

## Consequences

### Positive

- Opening a folder or choosing a large solution no longer freezes the window.

### Negative

- For a moment after a folder opens, the solution view shows the loader instead
  of the tree.
- Code that read the solution right after opening must wait for it.

### Neutral

- What the solution view shows once read is unchanged.

## Alternatives Considered

- **Read lazily, project by project, as nodes expand.** The tree needs every
  project to build its folders, and the language servers need the list of
  projects.
- **Cache parsed solutions on disk.** That would add invalidation for a cost
  that only the first read pays.

## Related

- [0633](0633-show-every-wait-with-the-core-loader-and-say-nothing-is-there-only-after-an-answer-said-so.md)
