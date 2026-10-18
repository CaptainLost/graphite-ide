# 0609. Toggle tool panels from a strip that ignores where they are docked

Status: proposed
Date: 2026-10-18

## Context

Graphite had no overview of its tools. A panel was switched through the tab bar
of the dock node it sat in, or through the View menu (0510). Several panels
start hidden (Git, Find in Files, References, Claude Sessions, Settings), so
nothing on screen said they existed. A panel can be docked anywhere: a node at
either side, the centre beside the editors (0493), a floating window on another
monitor. Terminals are windows of their own and have no View menu entry (0544).

The reader wanted one permanent icon per tool that shows whether it is open and
opens or hides it with one click, with every panel still dragged anywhere by its
tab.

## Decision

- Draw a column of icons, the tool strip, at the window's left edge, below the
  title bar and above the status bar. It sits in the dock space host window, in
  the slot `DockSpaceHost::Begin` keeps left of the dock space
  (`DockLayout::StripWidth`, `Core::ToolStripWidth`), so the dock space is only
  narrower and the layouts in `imgui.ini` keep their proportions. The status bar
  still spans the whole width.
- Give the strip one icon per panel the `PanelManager` holds, drawn with the
  panel's own `GetIcon()`, and one icon for all terminals. No list is kept by
  hand: a panel registered later is on the strip the day it is registered.
- Group the icons by the panel's menu group (0510), not by where the panel is
  docked: `navigate`, `git` and `ai` at the top; `tools`, with the terminals
  first, and `window` at the bottom. A group the strip does not know goes at the
  end of the top block, so no panel is dropped. Inside a group the order is
  registration order, as in the View menu.
- Send a click on a panel's icon to `PanelManager::Toggle`, which applies
  `ChoosePanelToggle` to whether the panel is visible and whether it was drawn
  on the last frame:
  - hidden: show it and bring it forward;
  - visible but behind another tab of its node, or a collapsed floating window:
    bring it forward, which expands it;
  - visible and drawn: hide it.

  Send a click on the terminals' icon to `TerminalWindows::ToggleFromStrip`
  (0608). A click is applied on the frame it is made, before the panels draw.
- Draw an icon as open while its panel is visible, wherever the panel is docked
  or floating. A panel behind another tab reads open too: two states, not three.
- Show the panel's title in the tooltip, and the shortcut bound to its
  `panel.<id>` command when there is one.
- Mark three states with a small dot at the button's corner, never a number:
  Changes in the accent while the working tree has changes; Problems in the
  diagnostic error colour while there are errors, in the warning colour while
  there are only warnings, from the counts the status bar shows; the terminals
  in a neutral colour while they are hidden with shells running.
- Leave docking alone. The tab bars stay, a panel is still dragged anywhere by
  its tab, a tab's close button still hides its panel, and the View menu's panel
  commands are unchanged.
- Keep the widget in Core and the entries in Graphite (0002):
  `Core::DrawToolStrip` draws groups of items, each an icon, a tooltip, an open
  state and an optional badge colour, and returns the id clicked;
  `BuildToolStripContent` in Graphite decides which items exist, their groups
  and the terminals' entry.
- Draw the strip's buttons in `DrawToolStrip`, not through `DrawIconButton`.
  This is a second exception to 0602, after the window's own buttons: a strip
  button needs an open fill in `SurfaceActive`, a 30 px square and a badge,
  none of which `DrawIconButton` has. The glyph is centred by the same
  `Core::DrawCentredGlyph` both use, so the two cannot drift apart there.

## Rationale

The reader asked for switches with state, and docking is what puts a panel where
the reader wants it. A strip that described the layout would have to follow
every drag or forbid some; one that ignores the layout keeps each icon in one
place whatever is moved where. The menu group is a fixed property of a panel,
which is what makes it a stable order, and it keeps the strip and the View menu
in step. The click rules follow from what an icon promises: a click must not
hide a panel the reader could not see.

## Consequences

### Positive

- Every tool is on screen and one click from open or hidden, the ones that start
  hidden included.
- A new panel needs nothing beyond its registration to get an icon.
- Saved layouts keep their proportions; the dock space only loses the strip's
  width.
- The click rules and the grouping are plain functions, tested without a window.

### Negative

- The dock space is 42 px narrower at 100 % scale for good, with no setting to
  take the strip away.
- An icon's place says nothing about where its panel is: a panel dragged to the
  right edge is still switched from the left.
- The strip reads visibility before the panels draw, so a panel hidden by its
  tab's close button, or shown from the View menu, changes its icon one frame
  later.
- Buttons that are only a glyph now have two looks: `DrawIconButton`'s and the
  strip's.

### Neutral

- The bars drawn over the dock space, such as the restricted-mode bar, span the
  whole width, so while one is shown the strip starts below it.
- The strip itself takes no keyboard focus.

## Alternatives Considered

- **Fixed tool areas, an icon on the side its panel belongs to.** This lost
  because each panel would be confined to its area, and panels must stay
  draggable anywhere, floating on another monitor included.
- **Toggles in the status bar.** This lost because the status bar already holds
  the branch, the server and the problem counts, a row of icons there crowds
  them, and a single row has no room to group the tools.
- **An activity bar that swaps what one side bar shows.** This lost because it
  turns a side of the window into one slot, so two tools cannot be seen side by
  side as docking allows now.
- **Icons in each dock node's header, for the panels docked there.** This lost
  because it describes the layout: an icon moves when its panel is dragged, and
  a hidden panel has no node to show its icon in.

## Related

- 0002, 0493, 0510, 0544, 0602, 0608
- `Core/src/Core/ImGui/ToolStrip.h`, `Graphite/src/App/ToolStripContent.h`,
  `Core/src/Core/UI/PanelManager.h`
