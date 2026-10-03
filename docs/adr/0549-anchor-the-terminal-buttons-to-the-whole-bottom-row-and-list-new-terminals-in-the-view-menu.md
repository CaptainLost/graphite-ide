# 0549. Anchor the terminal buttons to the whole bottom row and list new terminals in the View menu

Status: proposed
Date: 2026-10-03

## Context

0544 put a `+` and a `▾` on the tab bar of every dock node holding a terminal,
and of the node holding the Console panel, and left the menu bar without any way
to start a terminal. The default layout puts Console, Problems, Search and
References in one bottom row. With Console closed and only Problems left in that
row, the row lost its buttons, so whether a terminal could be started from the
bottom of the window depended on which of its panels happened to be open. Once
no terminal and no bottom-row panel was on screen, the shortcut was the only way
left, and nothing in the window said it existed.

## Decision

- The `+` and `▾` trail the tab bar of every node holding a terminal or any
  panel the layout puts in the bottom row (`DockLayout::Bottom`), not only the
  Console's. `TerminalDockTargets::RowWindows` carries those panels' window
  names. Where a new terminal docks is unchanged: beside the Console when no
  better place exists, as 0544 has it.
- View has `New Terminal` (`terminal.new`, with its shortcut printed) right
  after the Panels submenu, followed by a `New Terminal With` submenu listing
  the `terminal.shells` group. There is still no top-level Terminal menu.

This amends 0544 on those two points; the rest of it stands.

## Rationale

The bottom row is one place to the reader, so it should offer the same buttons
whichever of its panels is showing. A menu entry is the route that stays when
every tab bar is gone, and it is where a shortcut can be read rather than
remembered; it goes into View, beside the panels, so the menu bar keeps the
three menus a separate Terminal menu was rejected for crowding.

## Consequences

### Positive

- The bottom row keeps its `+` whichever of its panels is open.
- A terminal can be started from the menu bar, and the shortcut is shown there.

### Negative

- The buttons follow the panels, not the place: a bottom-row panel dragged to a
  side area takes a `+` with it, onto a tab bar that is not about terminals.
- With no shell discovered, `New Terminal` is disabled and `New Terminal With`
  opens empty.

### Neutral

- A node holding several bottom-row panels still gets one pair of buttons.

## Alternatives Considered

- **Track the bottom row's node itself.** This lost because ImGui gives a node
  a new id whenever it is split or merged, so the row would have to be found
  again after every drag, and a wrong guess would put the buttons somewhere else
  without any panel there to explain why.
- **A top-level Terminal menu.** Rejected again for the reason 0544 gives: it
  adds a menu for what is two entries.

## Related

- 0544
- `Graphite/src/Panels/TerminalWindows.h`, `Graphite/src/App/GraphiteMenu.cpp`
