# 0685. Close every file when another folder replaces the open one, asking first about unsaved changes

Status: proposed
Date: 2026-11-08

## Context

ADR 0522 inherited a replace-in-place behaviour: a folder opened over
another kept the old folder's documents open, "carried over", and only
stopped recording them for the new folder. A hands-on test pass on
2026-11-08 found what that does in daily use. File → Open Folder, Open
Solution and dropping a folder switched projects without a word about a
tab with unsaved changes; after a few switches one tab bar held files from
three projects, the exit question listed `Widget.h` with no hint of which
project it belonged to, and the old project's language server and error
counts stayed with its tabs.

Closing the folder (Close Workspace) already asked about unsaved changes
and closed every file.

## Decision

Treat replacing the open folder as closing it. Every request to open
another folder or a solution outside the open folder — the Open Folder and
Open Solution dialogs and a dropped folder or solution — goes through
`Workspace`'s replace gate. When no document has unsaved changes the gate
opens the next folder at once; otherwise it asks the close question, worded
"Opening another folder closes every open file. These have unsaved
changes:", with Save All, Discard All and Cancel. Cancel leaves the open
folder as it was; the other two open the next folder at the start of the
next update, outside every window being drawn.

When the folder is replaced, every document closes, as on Close Workspace.
Documents are no longer carried over, so the carry-over bookkeeping in
`DocumentWindows` is removed.

The folder opened at start (the command line or the last folder) does not
go through the gate: nothing is open yet that it could close. A solution
inside the open folder only selects it and closes nothing, so it does not
go through the gate either.

Terminals and Claude session windows are unchanged: a running shell or
session is a process, not a file, and is still carried over.

This amends ADR 0522's carry-over of documents.

## Rationale

The question the reader must answer is the same one Close Workspace and
exit ask, so it uses the same dialog with one line of context rather than a
new one. Closing the files rather than carrying them is what the reader
expects from "open another project", and it takes the old project's
language server traffic and error counts with it.

## Consequences

### Positive

- No unsaved change is dropped or hidden by opening another folder.
- One tab bar shows one project.

### Negative

- A reader who wanted a file of the old project open beside the new one
  has to open it again, by path, from the new folder.
- Open Folder now takes one more click when anything is unsaved.

### Neutral

- Terminals still carry over; a shell from the old folder keeps its working
  directory until closed.

## Alternatives Considered

### Ask, but keep carrying the documents over

Rejected: it answers the unsaved changes but keeps the mixed tab bar and
the old folder's language server state.

### Open the other folder in a new window

Rejected for now: Graphite runs one window per process, and a second
process per folder is a larger change than this fix.

## Related

- ADR 0522 — the carry-over this replaces for documents.
- `Graphite/src/Workspace/Workspace.h` — `SetReplaceGate`,
  `RequestOpenFolder`, `RequestOpenSolution`.
- `Graphite/src/App/GraphiteLayer.cpp` — `AskBeforeReplacingFolder` and the
  `ReplaceFolder` close purpose.
- `Graphite/src/App/FolderParticipants.cpp` — `DocumentsOfFolder::LetGo`.
