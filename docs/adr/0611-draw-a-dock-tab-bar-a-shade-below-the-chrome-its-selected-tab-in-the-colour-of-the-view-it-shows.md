# 0611. Draw a dock tab bar a shade below the chrome, its selected tab in the colour of the view it shows

Status: proposed
Date: 2026-10-18

## Context

Dock tab bars were drawn in the surface colour, with a 1 px overline on the
selected tab and a close-all button at the end of each node's bar. The selected
tab was the chrome colour, the colour of a side panel, but the view beside the
editors is the editor's own background, so the selected tab of a document did
not join the view under it. The overline was the only mark of the selected tab
and was faint. Tab labels carried two meanings: a git status colour (0556) and
a dim label for a preview tab.

The mockup draws the bar in the chrome itself (`#141518`). Inactive labels are
dim (`#7b828d`) and turn to the text colour under the pointer, with no fill.
The selected label is bright (`#eef1f5`). The tabs are square, with a 1 px line
on each one's right and along the bar's bottom, except under the selected tab.
Stage 8 first drew the bar in the chrome. The reader then saw that a selected
tab of a window in the chrome colour had the bar's own colour and read as black:
Explorer, Changes, Git, References and Find in Files. The reader asked for the
shade back.

## Decision

- Draw the tab bar a shade below the chrome: `Tab`, `TabDimmed` and the title
  bar colours are the background less 0.03 per RGB channel, `#0c0d10` in the
  dark scheme. A light scheme is shaded the same way. This reverses stage 8's
  flattening, by the reader's decision.
- Draw tabs square: `TabRounding` is zero.
- Fill the selected tab with the colour its window was begun with. A document's
  or a diff's window pushes the content colour (`#1b1d21`), and a chrome view
  keeps the chrome (`#141518`), so both stand out from the bar. Every other tab
  is the bar's colour. The fill is drawn over ImGui's tab (0628), so a hovered
  tab takes no fill.
- Draw an inactive tab's label dim, in `TextDisabled`. Draw it in the text
  colour while the tab is under the pointer. Draw the selected tab's label
  bright, in `Core::EmphasisText()`.
- Draw a preview tab's name in Inter Italic, under the same colour rules. Dim
  no longer means a preview.
- Show a file's git status as a 6 px dot in the status colour after the name,
  instead of colouring the label (amends 0556). An unsaved tab keeps ImGui's
  bullet where the close button stands.
- Lead a file's tab, a document's or a diff's, with its kind's badge (0617)
  instead of the generic glyph its window's name starts with. Lead every other
  tab with that glyph.
- Draw a 1 px line in the line colour on every tab's right edge, the selected
  one included. Draw one along the bar's bottom too, except under the selected
  tab. ImGui's own line under a bar cannot stop beneath one tab. So Core draws
  the bar's line after `DockSpace` (`Core::DrawDockTabBarLines`), and each tab
  paints over it and draws it again unless the tab is selected
  (`Core::DrawDockTab`). The line colour, `#24272c`, shows on the shaded bar as
  well as on the chrome and the content colour. `TabBarBorderSize` stays zero.
- Draw no overline: `TabBarOverlineSize` is zero.
- Draw no close-all button on a dock node: `DockingNodeHasCloseButton` is false.
  Each tab has its own close button (0628), and closing all stays in the tab
  context menu.
- Keep the side column's tab row, which takes this look too. The reader reversed
  the decision to hide it.
- Draw a tab bar inside a window, such as the review's pages, through ImGui as
  before: the selected page is the window's own colour and the others are the
  bar.

## Rationale

The reader judged the flat bar against the app and not the mockup. A panel's
selected tab is the chrome, so on a chrome bar nothing but its label told it
apart, and it read as a black gap. The shade keeps every selected tab apart
from the bar without a mark. Dim inactive labels and a bright selected one are
the mockup's own rule, and they read once dim is no longer the preview's sign.
Italic takes that sign over, and the dot takes over git status, so each
meaning has a channel of its own.

## Consequences

### Positive

- The selected tab joins the view it shows and stands out from the bar, in
  panels and in documents alike.
- Selection, preview and git status no longer share the label's colour.
- The tabs are told apart by a hairline, not a gap of darker bar.

### Negative

- The bar is darker than the chrome, a near-black band where the mockup has
  none.
- A git status is a small dot rather than a coloured name, so it is less loud.
- A preview tab differs from a permanent one only by its slant.
- Closing every tab of a node takes the context menu instead of one button.

### Neutral

- The title bar of a floating window, and of a modal dialog, takes the bar's
  shade, as it did before stage 8.

## Alternatives Considered

- **The bar in the chrome, as in the mockup.** Stage 8 drew it so first. It
  lost because a selected tab of a chrome-coloured window took the bar's
  colour and read as black.
- **An accent underline under the selected tab.** This lost because the fill
  already tells the selected tab and the mockup has no such mark.
- **Keeping the 1 px overline.** This lost because it is too faint.
- **ImGui's own line under the bar, `TabBarBorderSize`.** This lost because
  ImGui draws it under the whole bar in the global `TabSelected` colour and
  stops every tab's fill above it, so it would cut a document's selected tab
  from its editor.

## Related

- 0493, 0494, 0556, 0610, 0613, 0617, 0627, 0628
