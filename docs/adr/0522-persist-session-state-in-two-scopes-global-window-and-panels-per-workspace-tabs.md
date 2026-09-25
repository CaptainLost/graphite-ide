# 0522. Persist session state in two scopes: global window and panels, per-workspace tabs

Status: proposed
Date: 2026-09-25

## Context

Every launch opened a 1600×900 window with no documents and every panel back.
`imgui.ini` already remembered the dock layout and each document window's own
settings, and `AppSettings` already remembered the last folder, but nothing
remembered the window's placement, which panels were open, or what a folder
had been showing when it was last closed. A reader who left a dozen files open
across several folders lost all of them at every relaunch, docked back into
whatever the layout happened to be rather than where they had left it.

## Decision

Split what outlives a launch into two files, by scope.

`graphite.json`, beside the executable, holds what belongs to the window
itself: its placement (position, size, maximized) and which panels are
visible. It is process-wide — one window, one set of panels — so it has no
notion of a folder.

`<folder>/.graphite/workspace.json` holds what belongs to a folder: its open
file documents, which one is active, and for each one the caret and the top
visible line; its open Claude session windows, each with the view it showed
(conversation, cost or requests) and the title it had; and its open diff
windows, each as the commit (none for the working tree), the target and the
file's path. Paths inside are stored relative to the folder, matched against
the root element by element and ignoring case, since Windows paths are
case-insensitive; a path outside the folder falls back to being stored
absolute. The caret's column is stored in bytes into the line — the buffer's
own unit — and on restore is snapped back to the start of whatever UTF-8 code
point it lands inside, so a column saved mid-character never splits one open.
A tab whose file is already open when the folder's state is restored is left
alone: the reader is looking at it, and its own caret and view are worth more
than what was on disk.

A `StateKeeper` decides when to write, not what to write. It is shown a
snapshot every frame, compares it against what it last wrote, and writes a
part once it has gone on differing for two seconds — so dragging the window or
moving a caret does not write a file per frame, and a crash loses at most that
much. The two files are written independently: moving a caret never rewrites
`graphite.json`, and resizing the window never rewrites a workspace file. A
folder's part is also written outright, bypassing the delay, whenever that
folder closes or is about to be replaced — dropping a new folder onto the
window, or opening one over another, writes the old folder's state first,
before the new folder's tabs are captured. The old folder's documents are not
closed by this: today's replace-in-place behaviour keeps them open. They are
carried over, and a carried-over document is not recorded for the new folder
— a folder's tabs are written to that folder and never to another. A document
stops being carried over when its tab closes, when it is opened again from
the new folder, or when the new folder's own saved tabs name it too; from then
on it belongs to the folder open at the time. Session windows are carried
over by the same rule: a session is not tied to a folder, so its window stays
open, but it stops being recorded for the new folder until it closes, is
opened again from the Sessions panel, or is named by the new folder's own
saved state. Diff windows are not carried over — every diff closes when a
folder is opened or closed, since it is a question about one repository — so
the new folder's diffs are restored only after that close.
`StateKeeper` also flushes unconditionally on detach, so a normal exit never
depends on the two-second delay.

A session window is restored before its transcript is known: only the session
index, scanning Claude's projects directory, maps an id to a file. Until the
index reports it, the window shows "Looking for this session…" under the
title it had, rather than the "Waiting for Claude to write the session's first
lines" a newly started session shows. The Sessions panel starts the index for
this even if the list itself has never been shown. Once the index has
finished its first full scan, a restored session it does not know is closed
and logged, as a restored file tab whose file is gone is skipped. The saved
view is selected once, on the first frame the window's tab bar is drawn.

A diff's path is stored as the window holds it — relative to the
repository's working directory, as git reports it — rather than relative to
the folder, since the window's identity, and so its place in `imgui.ini`, is
spelled from it. A diff whose commit does not parse as a full forty-digit id,
whose target is not one of the stored names, whose path is empty, or whose
commit and target disagree (a commit's diff with no commit, the working tree's
with one) is dropped on reading. Only the first working-tree diff is reopened:
the git model keeps one slot for it.

Dock placement is not duplicated into either file: `imgui.ini` already keys
each document, session and diff window by its identity, and a window restored
from the last launch skips the forced docking a newly opened one gets when
`imgui.ini` remembers a place for it that still exists — floating, or a dock
node that is still there — so it lands where `imgui.ini` already has it.
Restored windows do not take focus; which tab of a node is in front is what
the node itself remembers. Unsaved buffer contents and the git view are not
restored — they are either recoverable from disk or not worth the shape a
format would have to take to hold them.

## Rationale

The two files differ in what governs their lifetime: the window is one per
process and closes when the application does, a folder can be opened,
replaced or moved independently of the window around it. Folding both into
one file would tie a folder's tabs to whichever window last had them, or force
every workspace file to repeat the window's placement. Keeping the delay and
the write itself in one class, told only when a value last differed from what
is on disk, is what lets every caller just hand `StateKeeper` a snapshot
without separately tracking whether anything changed.

## Consequences

### Positive

- A crash loses at most two seconds of state, not the whole session.
- A moved or cloned folder keeps its tabs: paths are relative to the folder,
  not to the machine.

### Negative

- Closing a folder and reopening it in the same run restores its tabs into
  the central node, because closing documents clears their `imgui.ini`
  settings and the restore has nothing else to defer to.

- Two files means two failure surfaces and two places a reader's edit to one
  can leave the other stale — a `graphite.json` deleted by hand still leaves
  every folder's `workspace.json` untouched, and vice versa.
- The caret's column being a byte offset rather than a code point index means
  a workspace file edited by hand, or written by a future version with a
  different encoding assumption, can land the caret mid-character if the
  snapping logic is ever bypassed. It is not bypassed today, but the format
  itself does not prevent it.
- A folder dropped over another leaves the old folder's documents open rather
  than closing them, which is a choice this record inherits from existing
  behaviour rather than one it makes. They stay open but are not recorded for
  the new folder, so a relaunch into the new folder does not bring them back
  — a reader who kept working in a carried-over tab finds it in the folder it
  came from, not in the one that was open when they closed the application.
  The same holds for carried-over session windows.
- A restored session window whose session the index cannot find stays open,
  looking, until the index's first full scan is in; when Claude's projects
  directory is missing altogether that scan never runs and the window keeps
  looking.

### Neutral

- Unsaved buffer contents and the git view are not restored. Nothing here
  rules out adding them later; they are simply outside what two seconds of
  delayed, per-part writing was built to hold.

## Alternatives Considered

### One file for everything

A single state file, keyed by folder, kept beside the executable or in a
user-level directory. Rejected because it ties a folder's tabs to whichever
machine or install wrote them, defeating the reason a folder's own
`.graphite/workspace.json` is worth having: a folder that is moved, cloned, or
opened by a different checkout of the application should keep its tabs
without carrying a lookup key anywhere else.

### Store the caret as a code point column

Counting UTF-8 code points rather than bytes would make the stored column
independent of the buffer's own representation. Rejected because the buffer
already measures and seeks by byte offset everywhere else in the editor; a
code-point column would need its own conversion at every read and write for a
value that is thrown away the moment the line changes upstream of the caret
anyway. Snapping a byte column to the nearest code point start on restore
costs less than carrying two representations of the same position.

## Related

- `Graphite/src/Persistence/PersistedState.h`, `StateCodec.h`/`.cpp` and
  `StateKeeper.h`/`.cpp` — the two file formats and the write-delay class.
- `Graphite/src/Persistence/CaretRestore.h` — where a saved line and byte
  column land in the document as it is now.
- `Graphite/src/Editor/DocumentWindows.cpp` — `CaptureState` and
  `RestoreState`, including the already-open skip, and
  `CarryOverOpenDocuments`, which keeps a replaced folder's tabs out of the
  new folder's state.
- `Graphite/src/Panels/SessionWindows.cpp` — `CaptureState`, `RestoreState`,
  `CarryOverOpenSessions` and `SettleRestored`; `SessionsPanel.cpp` settles
  restored windows once the index's first scan is in.
- `Graphite/src/Git/DiffWindows.cpp` — `CaptureState` and `RestoreState`.
- `Graphite/src/UI/RememberedPlace.h` — whether `imgui.ini` remembers a place a
  restored window can go back to, shared by all three kinds of window.
- `Graphite/src/App/GraphiteLayer.cpp` — `SubscribeToWorkspaceEvents`, where a
  folder being replaced writes the old folder's state before the new one's
  tabs are captured, and diffs are restored after every diff is closed.
