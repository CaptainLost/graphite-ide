# 0609. Hide all terminals together with their shells kept running

Status: proposed
Date: 2026-10-18

## Context

Since 0544 each terminal is an ImGui window of its own, owned by
`TerminalWindows`, and closing its tab ends its shell. A panel can be hidden and
shown again with its place in the layout kept, but a terminal could only be
closed: the one way to get it out of sight was to end the shell in it, or to
bury its tab behind another. A build, or a Claude session run in a terminal
(0519), is work the reader wants kept going while looking at something else,
and a row of terminals takes a share of the window's height the editor could
use.

The tool strip at the window's left edge gives each panel an icon that shows
and hides it, and gives terminals one icon of their own. That icon needs a
state to switch, and the state has to say what happens to the shells, to the
starts made while it is set, and across a restart.

## Decision

- `TerminalWindows` keeps one hidden flag over every terminal, whichever folder
  it belongs to. While it is set no terminal window is submitted, so no `Begin`
  is made for any of them. `Update` still drains every session, so the shells
  keep running and their output keeps arriving. A shell that exits while hidden
  is removed as it would be if shown, and the tab-bar `+` and `▾` of 0544 keep
  working on the nodes that are still drawn, such as the Console's.
- Hiding does not move a terminal. ImGui keeps the dock id of a window that is
  not submitted, so a terminal shown again comes back in the node it left, and a
  terminal started while hidden is placed by 0544's rule on the first frame it
  is shown.
- A click on the Terminal icon goes to `TerminalWindows::ToggleFromStrip`, which
  applies `ChooseTerminalStripAction` from `TerminalRules` to the number of
  terminals, the flag and whether any terminal was drawn on the last frame:
  - no terminal: clear the flag and start the default shell, as `terminal.new`
    does;
  - shown, at least one drawn: set the flag;
  - shown, every one behind another tab: bring forward the terminal focused
    last;
  - hidden: clear the flag and bring forward the terminal focused last.

  The terminal focused last is chosen by `TerminalToBringForward`: the one that
  had focus, or the last one open when that one has been closed since.
- The icon reads open while the flag is clear and a terminal exists. With the
  flag set and a terminal running it reads closed with a neutral dot. With no
  terminal it reads closed whatever the flag says (`TerminalStripLookOf`).
- A start the person asks for clears the flag before the start is attempted,
  so a start that fails still shows the terminals already open: `terminal.new`
  and each `terminal.new.<shell>`, a node's `+` and `▾` and a tab's New Terminal
  menu, the Explorer's Open in Terminal, and a program run in a terminal
  (0519). View → Reset Layout clears it too, and docks the terminals again by
  0544's rule.
- A start Graphite makes on its own leaves the flag alone: the startup shell
  opened with no folder, a folder's saved shells, and the default shell a folder
  with no saved list opens. With the flag set they come up hidden. The flag is
  cleared at each entry point a person reaches, never inside `StartShell`, which
  the automatic starts reach too.
- The flag is kept in `graphite.json` beside the panels' visibility, in the
  global scope of 0522, so it survives a restart, Close Workspace and a folder
  switch the way a hidden panel does. A folder does not remember a hidden state
  of its own.

## Rationale

The reader hides terminals to get the room back, not to stop the work in them,
so the flag must leave the shells alone, and not submitting a window is the way
ImGui offers to take it off screen without closing it while keeping its place.
One flag over all terminals matches the one icon. A start the person asks for
says they want a terminal now, which a hidden one would keep from them; a start
Graphite makes on its own was not asked for, and showing it would undo the
reader's choice each time a folder opens.

## Consequences

### Positive

- A long build or a Claude session can run out of sight and come back, output
  intact, in the place it left.
- The click rules and the icon's look are plain functions over a count and two
  flags, tested without a window or a shell.
- A folder restored while terminals are hidden opens its shells without
  changing what the reader sees.

### Negative

- Hiding is all or nothing: one terminal cannot be hidden on its own, and
  bringing one back shows them all.
- A hidden terminal is still a running process, and the dot does not say how
  many there are.
- Bringing a terminal forward focuses its window, which selects its tab but does
  not expand a collapsed floating node, so a click over a collapsed terminal
  changes nothing on screen.

### Neutral

- Closing a terminal's tab still ends its shell; hiding is a separate action,
  reached from the strip.
- View still has no entry that shows or hides the terminals. Its New Terminal
  and New Terminal With (0549) start a shell, which shows the terminals again
  as any start the person asks for does.

## Alternatives Considered

- **A hidden state per terminal.** This lost because the strip has one icon for
  all terminals and no list to show a single one back from, and a per-terminal
  state would need a rule for every mix of hidden and shown terminals carried
  from one folder to the next.
- **Keep the windows submitted and collapse them.** This lost because a docked
  window cannot be collapsed on its own, and a submitted window keeps its tab
  and its share of the node, which is the room the reader wanted back.
- **Clear the flag on every start, Graphite's own included.** This lost because
  a folder's restore would then show the terminals the reader had hidden each
  time a folder opens, and the startup shell would do the same at every launch.
- **Keep the flag per folder in `workspace.json`.** This lost because terminals
  carried over from one folder to the next (0544) would need a rule for whose
  flag wins, and the panels' visibility is already global.

## Related

- 0519, 0522, 0544, 0549
- `Graphite/src/Terminal/TerminalWindows.h`, `Graphite/src/Terminal/TerminalRules.h`
