# 0544. Draw each terminal as a window of its own, docked beside the last one used

Status: proposed
Date: 2026-10-01

## Context

Terminals were tabs of an inner `BeginTabBar` inside one `TerminalPanel`
window. ImGui docks whole windows only, so a terminal could not be dragged
beside the editor, split off to the side, or pulled into an OS window of its
own; it stayed inside the one panel that held them all. Documents, diffs and
Claude sessions are already windows of their own, each owned by a class that
draws any number of them (0494), and their tabs move freely.

Two things have to keep working once terminals are windows. A shell is started
from somewhere, and a window that appears needs a place to appear in. And a
folder's layout is remembered by `imgui.ini`, keyed by window name, so a
terminal that comes back after a restart has to come back under the same name.

## Decision

- `TerminalWindows` owns the terminal sessions and draws one ImGui window per
  terminal. A window's identity is `###terminal-<Id>`, where `Id` is a
  `Core::Uuid` string; the text before `###` is the visible title and may change.
- The Terminal panel is removed, and with it the `terminal` entry in the panel
  registry and the bottom layout. There is no Terminal menu either. A shell is
  started from the place a terminal is wanted:
  - a `+` and a `▾` trail the dock tab bar of every node that holds a terminal,
    and of the node that holds the Console panel. They are added through ImGui's
    `DockNodeBeginAmendTabBar`, so ImGui's own bar is extended rather than drawn
    over. `+` opens the default shell and `▾` lists the discovered shells; the
    new terminal docks into that node;
  - a terminal tab's right-click menu has `New Terminal` with the same shell
    list, docking into that tab's node;
  - a terminal that floats on its own keeps a tab bar
    (`ImGuiWindowClass::DockingAlwaysTabBar`), so it has the same buttons and
    menu as a docked one;
  - `Open in Terminal` on a folder or file in the Explorer starts a shell in
    that folder (`OpenTerminalRequested`), the file's folder for a file;
  - `terminal.new` opens the default shell and is bound to Ctrl+Shift+`, and
    each discovered shell has a `terminal.new.<slug>` command, keyed by the
    profile's name rather than its position, for the command palette.
- A new window is docked on its first frame only, afterwards `imgui.ini` owns
  its place. The target is the first that exists: the node whose button or menu
  asked for it, else beside the terminal focused last (into its dock node, or
  onto it as a tab when it floats), else the node of the Console panel, else
  the node of any other terminal, else a new bottom row made by splitting the
  central node downward, else the central node. The new row is what a start
  makes when no terminal and no Console is on screen; no tab bar is left to
  carry a `+` then, and the shortcut is the way back to a terminal.
- Shell terminals are kept per folder (0522) as `restore.terminal.*`: `Id`,
  profile name and starting directory. A folder with no saved list is told apart
  from a folder with an empty one: absent opens one default shell, empty opens
  none. The bookkeeping (what to capture, what to skip, what to start) is in
  `TerminalRules`, which has no ImGui and starts no process.
- Terminals started by a command (0519) are never kept, and neither are
  terminals carried over from a folder that another one replaced.
- The id is a `Core::Uuid`, not a counter, so two folders' terminals never share
  an `imgui.ini` entry and a restored id cannot collide with one already open.

## Rationale

Docking is a property of windows, so the only way for a terminal to dock like a
document tab is to be a window. Placing a new one beside the terminal last used
puts it where the reader's attention already is, and a node's own `+` says more
than which terminal had focus last; the fallbacks cover the first terminal of a
session. Remembering the profile and directory rather than the
process keeps the feature small and honest: the layout returns, the shell is
new.

## Consequences

### Positive

- A terminal can be dragged, docked, split and floated like any other tab, and
  its place survives a restart.
- Close, Close Others and the other tab context menu entries reach terminals
  through the same path as every other window (0528).
- The restore rules are testable without a shell, a window or a frame.

### Negative

- Shell processes and scrollback do not survive a restart. A terminal comes back
  in the same place with a fresh process in the same directory.
- A `panel.terminal.visible` key lingers in global state from earlier runs. It
  is ignored, since `PanelManager::SetVisible` returns false for an id it does
  not know, but nothing removes it.
- Docking a new window onto a floating terminal takes one frame, because the
  window to be docked must exist before the dock can be queued. The new window
  is drawn over its target for that frame so the move is not seen.
- The tab-bar buttons are added to a bar that was drawn this frame, so they
  appear one frame after a node first shows.
- A bottom row made again by splitting the central node moves the open documents
  up at once, to give the row its share of the height.

### Neutral

- There is no longer a single Terminal view-menu entry, nor a menu-bar menu.
  Which terminals are open is whatever windows exist.

## Alternatives Considered

- **Keep the panel and add an "Undock" action per tab.** This lost because a tab
  in the inner strip could not be dragged out, so the action would be the only
  way, and a terminal would be either a tab or a window depending on how it
  got there: two ways to be a terminal to keep consistent.
- **Keep the panel as a launcher beside free terminal windows.** This lost
  because the panel would be an empty tab whose only job is a shell chooser and
  a new button, which the tab-bar buttons and the keymap do better.
- **Always dock new terminals at the bottom.** This lost because a reader who
  has moved their terminals elsewhere would find each new one back where they
  took the last one from.
- **A top-level Terminal menu.** This was built first and rejected after use:
  terminal creation in the menu bar was not wanted.
- **A terminal button in the status bar.** This lost because, although it is
  always present whatever the layout, it is away from where terminals are; the
  tab-bar buttons plus the shortcut cover the same cases.

## Related

- 0494, 0519, 0522, 0528
- `Graphite/src/Panels/TerminalWindows.h`, `Graphite/src/Panels/TerminalRules.h`
