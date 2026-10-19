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

- Draw the tab bar in the chrome colour itself, as the mockup does: `Tab`,
  `TabDimmed` and the title bar colours are `WindowBg`. An inactive tab is the
  bar. An earlier version of this record shaded the bar 0.03 per RGB channel
  below the chrome; there is no shade.
- Draw tabs square: `TabRounding` is zero.
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
  selected page is the window's own colour and the others are the bar.

## Rationale

The mockup draws the tab bar in the chrome itself, so the bar is one with the
title bar, the strip and the side panel around it, and no band in the window is
darker than the chrome. A document's selected tab is the content colour, lighter
than the bar, so it still reads without a mark. A panel's selected tab is the
chrome as well and so has the bar's own colour. That is accepted: in a panel
node the label tells the selected tab once inactive labels are dimmed, and the
side column, where most panel tabs sit, is to lose its tab row.

## Consequences

### Positive

- The selected tab joins the view it shows, in panels and in documents.
- A document's selection needs no extra mark.
- The bar follows the scheme's chrome colour and has no constant of its own.

### Negative

- Closing every tab of a node takes the context menu instead of one button.
- A panel's selected tab has the bar's colour, so in a panel node nothing but
  the label tells it from the others.
- An inactive tab under the pointer takes the selected tab's colour, so while
  hovered it looks like the selected tab.

### Neutral

- The title bar of a floating window is the chrome colour too, and so is a modal
  dialog's, which makes a dialog's title bar darker than its body.

## Alternatives Considered

- **An accent underline under the selected tab.** This lost because ImGui draws
  dock tab bars and offers no such mark.
- **Keeping the 1 px overline.** This lost because it is the only mark of the
  selected tab and is too faint.
- **A bar a shade below the chrome, 0.03 per channel.** This was this record's
  first decision and lost because the mockup draws the bar in the chrome, and
  the shade made the bar the darkest band in the window, a near-black strip over
  the editor and every panel.

## Related

- 0493, 0494, 0556, 0610
