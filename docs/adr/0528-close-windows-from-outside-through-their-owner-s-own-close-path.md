# 0528. Close windows from outside through their owner's own close path

Status: accepted
Date: 2026-09-27

## Context

Four owners draw windows into the dockspace: `PanelManager` for panels,
`DocumentWindows` for files, `DiffWindows` for diffs and `SessionWindows` for
Claude sessions. Each closes its windows its own way. A panel is hidden and
can be reopened from the View menu. A diff forgets its cached texts. A session
stops its reader. A document with unsaved changes is not closed at all until
the user has answered a question about it. Every one of these paths starts in
the same place: the `open` flag that `ImGui::Begin` clears when the tab's
close button is clicked.

A tab's context menu offers Close, Close Others, Close to the Right and Close
All. The last three reach tabs of any owner that share the clicked tab's dock
node, so the menu has to close windows it does not own. The menu is drawn
partway through a frame, after some owners have already drawn theirs.

## Decision

- Code that wants a window closed, and does not own it, asks through
  `Core::TabCloseRequests::Request`, by the window's name.
- Every owner takes its window's name with `Take` right after that window's
  `Begin`, and on a hit sets `open = false`, exactly as if the tab's close
  button had been clicked. Everything after that is the owner's usual close
  path, unchanged.
- Nothing outside an owner removes that owner's entry directly.
- A request is due one frame after it is asked. `GraphiteLayer` owns the one
  `TabCloseRequests` and calls `EndFrame` once all owners have drawn. That
  drops the requests nobody took, since those windows went some other way,
  and makes the requests of this frame due.

## Rationale

The owner's close path is the only one that knows what closing costs, whether
that is a question about unsaved work, a cache to drop or a thread to join.
Routing a menu's close through the same `open` flag that the close button
clears means there is one path to keep correct rather than two. Deferring
every request by one frame makes the order in which owners draw irrelevant.

## Consequences

### Positive

- A menu cannot skip the unsaved-changes question or an owner's cleanup.
  Closing a modified file from a menu asks about it, one question per file
  in turn, the same as its close button.
- The menu knows nothing about owners. It works on window names, so it
  closes a panel, a file, a diff and a session alike.

### Negative

- A request lands one frame late, so a closed tab stays on screen for one
  more frame after the menu item is picked.
- A new window owner must call `Take` after its `Begin`. Until it does, its
  tabs ignore Close from the menu and give no sign of it.

### Neutral

- Requests are matched by window name, so two windows must not share a name.
  ImGui already requires this, because the name is also the window's identity.

## Alternatives Considered

- **Let the menu remove owners' entries directly.** Each owner would expose
  a close-by-name call for the menu to invoke. This lost because it opens a
  second close path next to the close button. That path has to repeat the
  unsaved-changes question and each owner's cleanup, and a copy that drifts
  closes a modified file without asking.
- **Act on requests in the same frame.** The menu is drawn inside some
  owner's window, after the owners before it have drawn. A request for one of
  their windows would be missed this frame. Carrying it over only for them
  makes the timing depend on draw order. Deferring every request by one frame
  treats all owners the same.
- **Register a close callback per window with a central registry.** This
  would give the menu something to call for each window. It lost because it
  adds a registration step every owner must keep in sync with its windows'
  lifetimes. Taking the name after `Begin` needs no state beyond the frame.

## Related

- `Core/src/Core/ImGui/TabCloseRequests.h`, `Core/src/Core/ImGui/TabContextMenu.h`
