# 0628. Make dock tab bars thirty-two pixels with the padding agreed in five places

Status: proposed
Date: 2026-10-19

## Context

The mockup's tab bar is 32 px tall. Its tabs are square and touch one another,
and the first one starts at the node's left edge. A tab's label is padded 12 px
on the left and 8 px on the right. Its badge, name and close button are 6 px
apart, and the close button is a 16 px box.

ImGui gives a dock tab bar no height of its own. `DockSpace` lays each node's
bar out at the font size plus twice `FramePadding.y`. A docked window's `Begin`
sizes its title bar the same way, and the window's content starts below that
title bar. The two agree only when both see the same padding.

With `FramePadding.y` at 5 for 26 px fields (0627), both would be 26 px. Pushed
around `DockSpace` alone, the bar would grow under the window and the content
would not move down (DockSpaceHost's warning on the menu bar height).

`FramePadding.x` and the window border also set the first tab's inset, 9 px,
and how far ImGui's own close button sits from the tab's end. Neither the close
button's reach nor its visibility has a style var. ImGui spaces tabs by
`ItemInnerSpacing.x`, 4 px. It sizes a tab from the window's name, and draws the
name in one face for every tab of a node.

## Decision

- **Height.** Make a dock tab bar `round(Scaled(32))` tall.
  `Core::DockTabBarFramePaddingY()` is that height less the font size, halved:
  8 at the 16 px interface size. Push it in five places, so that what lays out
  a bar and what sits below it agree:
  1. around `DockSpace`, through `Core::DockTabBarStyle`;
  2. around the `Begin` of every docked window, through `Core::BeginDockedWindow`,
     which `Graphite::BeginTabWindow` and `Core::PanelManager` call;
  3. and 4. around each of the terminal's two tab-bar buttons, `+` and the shell
     chevron, which are tabs of the bar;
  5. in `Core::DrawDockTab` and `Core::DrawDockTabBarLines`, which draw over the
     bar from the rects ImGui laid out at that height.
- **Flat tabs.** Inside `DockSpace`, set `FramePadding.x`, `ItemInnerSpacing.x`
  and `WindowBorderSize` to 0. Then the first tab starts at the node's edge and
  each tab starts where the one before it ends. Set
  `TabCloseButtonMinWidthSelected` and `TabCloseButtonMinWidthUnselected` to
  `FLT_MAX` for that call, and back after it, so ImGui's own close button never
  shows.
- **Width.** Each tab asks for its width every frame through its
  `ImGuiTabItem::RequestedWidth`. That width is 12 + lead + 6 + name (+ 6 + 6
  for a dot) + 6 + 16 + 8 px. ImGui reads it at the next layout and forgets it
  once the tab is drawn.
- **Drawing.** After its window's `Begin`, `Core::DrawDockTab` amends the
  node's tab bar and draws the tab over ImGui's. It paints an opaque fill:
  the window's own `TabSelected` for the selected tab, the bar's `Tab` for the
  others. It then draws the lead, the name in the look's face, the dot, the
  close button and the unsaved bullet. A tab's look comes from its owner,
  `Core::DockTabLook`, or from the window's name: a glyph, then the title.
- **Close.** The close button is ours: a 16 px box with 3 px corners and an
  11 px Lucide `x`, dim at rest and the text colour under the pointer, on the
  hover surface. It shows on the selected tab and under the pointer. Its click
  and a middle click on the tab ask `Core::TabCloseRequests` to close the
  window, as the tab's context menu does. An unsaved tab shows ImGui's bullet in
  its place until the pointer comes.
- **The terminal's buttons.** They ask for their width and draw their glyph
  centred. They are clear at rest and take the hover surface under the pointer,
  so the line under the bar runs beneath them.

## Rationale

The padding is the only lever ImGui gives over a dock bar's height, and it has
to be the same wherever the bar is measured, so it lives in Core helpers rather
than in each caller. Drawing over ImGui's tab, rather than patching ImGui, keeps
ImGui's layout, reordering, dragging and context menu. It still gives the
mockup's padding, badge, italic name, dot and close button, which ImGui cannot
draw.

## Consequences

### Positive

- Bar, tabs and content line up at 32 px, at every scale.
- One look serves documents, diffs, panels, terminals, sessions and reviews.
- A tab's width is exact, whatever face its name is drawn in.

### Negative

- A tab drawn this frame was laid out from the width it asked for last frame.
  A change of width, such as a dot appearing, lands a frame late.
- A node dragged out to float is laid out by ImGui in `NewFrame`, with the
  global padding: its bar is 26 px under 32 px title bars. ImGui's close button
  shows there under our fill, and its first tab is inset.
- Closing through the close button or a middle click takes effect a frame
  later than ImGui's own close did, as the context menu's always has.
- A docked window whose owner does not call `DrawDockTab`, such as ImGui's own
  tools, keeps ImGui's look with no close button.

### Neutral

- The trailing buttons' glyphs are drawn in the text colour, as before.

## Alternatives Considered

- **A patch to ImGui's tab bar.** Not allowed (`vendor` is built as it comes),
  and it would need carrying through every ImGui update.
- **Padding the window's name with spaces to reserve room for the badge.** This
  lost because spaces cannot give an exact width, and the name would still be
  drawn in one face for every tab.
- **The global `FramePadding.y` at 8.** This lost because every field would then
  be 32 px tall.

## Related

- 0610, 0611, 0613, 0626, 0627
