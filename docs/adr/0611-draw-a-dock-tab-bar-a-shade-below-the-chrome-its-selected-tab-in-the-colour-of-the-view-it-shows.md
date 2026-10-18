# 0611. Draw a dock tab bar a shade below the chrome, its selected tab in the colour of the view it shows

Status: proposed
Date: 2026-10-18

## Context

Dock tab bars were drawn in the surface colour, with a 1 px overline on the
selected tab and a close-all button at the end of each node's bar. The selected
tab was the chrome colour, the colour of a side panel, but the view beside the
editors is the editor's own background, so the selected tab of a document did
not join the view under it. The overline was the only mark of the selected tab
and was faint. Tab labels already carry two meanings: a git status colour
(0556) and a dim label for a preview tab.

## Decision

- Draw the tab bar in the chrome colour minus 0.03 per RGB channel, clamped at
  zero. An inactive tab is the bar.
- Draw the selected tab in the chrome colour. A content window pushes the content
  colour before `Begin`, so the selected tab of a document merges with its
  editor. Pushing before `Begin` follows `TabTextColour`, which sets a tab's
  label colour the same way.
- Draw a hovered tab in the selected tab's colour: `TabHovered` is the chrome
  colour, and a content window pushes the content colour for it too. ImGui fills
  a hovered tab with `TabHovered` whether it is selected or not, so any other
  colour lifts a selected tab off its view under the pointer.
- Draw no overline: `TabBarOverlineSize` is zero.
- Draw no line under the bar or between tabs: `TabBarBorderSize` is zero and
  `TabBorderSize` stays zero. This replaces the planned hairline stopping beneath
  the selected tab, which ImGui cannot draw per tab: it draws one line under the
  whole bar in the global `TabSelected` colour, before a window's own colours
  apply, and stops every tab's fill above that line, so a document's selected
  tab would be cut from its editor by a line in the chrome colour.
- Draw no close-all button on a dock node: `DockingNodeHasCloseButton` is false.
  Closing all stays in the tab context menu.
- Keep tab labels in their own colours. An inactive tab is not dimmed, because a
  dim label already means a preview tab (0556).
- Draw a tab bar inside a window, such as the review's pages, the same way: the
  selected page is the window's own colour and the others are the shade.

## Rationale

A bar a shade darker than the chrome makes the selected tab, which is the chrome
or the content colour, the lightest part of the strip, so the selection reads
without a mark. Tying the shade to the palette keeps a light scheme working on
its own.

## Consequences

### Positive

- The selected tab joins the view it shows, in panels and in documents.
- The selection needs no extra mark.
- The shade follows the scheme's chrome colour.

### Negative

- Closing every tab of a node takes the context menu instead of one button.
- The difference between bar and selected tab is small, 0.03 per channel, and
  may be hard to see on some displays.
- An inactive tab under the pointer takes the selected tab's colour, so while
  hovered it looks like the selected tab.

### Neutral

- The title bar of a floating window uses the same shade, and so does a modal
  dialog's, which makes a dialog's title bar darker than its body.

## Alternatives Considered

- **An accent underline under the selected tab.** This lost because ImGui draws
  dock tab bars and offers no such mark.
- **Keeping the 1 px overline.** This lost because it is the only mark of the
  selected tab and is too faint.
- **A bar in the chrome colour.** This lost because the selected tab of a panel
  would be the bar's own colour.

## Related

- 0493, 0494, 0556, 0610
