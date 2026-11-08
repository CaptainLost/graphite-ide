# 0687. Keep a .gitignore of everything inside each folder's .graphite

Status: proposed
Date: 2026-11-08

## Context

Graphite keeps per-folder state in `<folder>/.graphite`: the open tabs
(ADR 0522), the branch reviews, and the workspace analysis. A hands-on test
pass on 2026-11-08 opened fresh repositories and found `.graphite/` listed
as untracked in the Explorer and the Changes view and counted in the status
bar's "N changes", in a restricted folder too; Stage all then put
Graphite's own state into the reader's first commit. Nothing in the folder
told git to leave it alone, and asking every reader to add it to their
`.gitignore` puts Graphite's housekeeping into their project.

## Decision

Create the state directory only through `EnsureFolderStateDirectory`, which
also writes `.graphite/.gitignore` holding `*` whenever that file is
missing. A `.gitignore` that ignores everything ignores itself too, so git
lists nothing under `.graphite`, while the reader's own `.gitignore` and the
repository stay untouched. An existing `.gitignore` there is never
rewritten, so a reader who chose to track part of it keeps their choice.
Opening a folder whose `.graphite` already exists without one adds it at
once, for directories written by earlier builds; a folder with no
`.graphite` is not given one by opening it.

The workspace settings, the review store and the analysis store all create
the directory through this function; `OwnStateChanges` names it through
`FolderStateDirectory`.

## Rationale

A nested ignore-everything file is the one place git honours that belongs
to Graphite alone: it needs no edit to the project's files, works for every
git client, and goes away with the directory.

## Consequences

### Positive

- `.graphite` no longer shows as a change, in any git tool, and cannot be
  committed by Stage all.

### Negative

- A reader who wants to share part of `.graphite` has to edit or delete the
  generated `.gitignore` first, and a deleted one is written again at the
  next write.

### Neutral

- Restricted mode still writes the folder's tab state; it is now invisible
  to git.

## Alternatives Considered

### Add `.graphite/` to the repository's `.git/info/exclude`

Rejected: it covers only the repository that is open, not a folder that
becomes a repository later or another clone of it, and it edits git's own
files.

### Keep the state outside the folder

Rejected here: ADR 0522 chose the folder so a moved or cloned folder keeps
its tabs; that trade is not reopened by this fix.

## Related

- ADR 0522 — what lives in `.graphite`.
- `Graphite/src/Persistence/FolderStateDirectory.h`/`.cpp`.
- `Graphite/src/Workspace/Workspace.cpp` — `OpenFolder` adds the file to an
  existing directory.
