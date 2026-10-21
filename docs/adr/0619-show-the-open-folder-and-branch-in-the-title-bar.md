# 0619. Show the open folder and branch in the title bar

Status: proposed
Date: 2026-10-19

## Context

The title bar, after the menus, carried the label "Graphite" and the version in
the disabled-text colour. It told the reader nothing they could not read in the
About dialog, while the two facts a window of an IDE is most often told apart by
sat elsewhere: the open folder in the Explorer, the branch in the status bar.

The bar is the drag region between the menus and the window buttons
([0477](0477-draw-the-title-bar-in-the-application.md)). Anything drawn there
that is not an item leaves the hit test as it was.

The mockup the interface is drawn to sets the rest of the bar as well: menus
whose fill is 22 px tall in a 28 px bar, window-button glyphs of 14 px in an
icon grey, a red close button with a white glyph, and a 17 px logo. The logo's
own colours are light greys, which almost vanish on the light scheme's chrome.

## Decision

Draw, after the menus, the open folder and its branch instead of the version.

- **Pieces, in order:** a 1 px divider in the line colour (`ImGuiCol_Separator`);
  the folder glyph `ICON_LC_FOLDER` in the disabled-text colour and the folder's
  name in the text colour; a gap; `ICON_LC_GIT_BRANCH` in the disabled-text
  colour and the branch in the text colour. A glyph and its text are 6 px apart.
  The divider is half the bar tall, 14 px; every piece is centred vertically in
  the bar.
- **Spacing:** 12 px between the menus and the divider and between the divider
  and the folder, 16 px between the folder and the branch glyph, all scaled
  ([0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md)).
- **The folder** is the last part of the workspace root's path; a root with no
  last part, such as a drive, is shown whole.
- **The branch** is worded by `GitBranchLabel`, which takes `GitStatusText`'s
  branch and removes the glyph that leads it, so the bar and the status bar
  never disagree. It is absent where the status bar shows no repository.
- **With no folder open** nothing is drawn after the menus. A folder without a
  repository shows the divider and the folder alone.
- **Fit:** `Core::LayOutCaption` places the pieces and keeps each clear of the
  window buttons by 12 px. The branch goes first, then the folder with the
  divider. A piece is never shortened, and none is drawn over the menus.
- **The version** is no longer in the bar. It stays in the About dialog.

`TitleBar::Render` takes a `TitleContext` of two optional strings, the folder
and the branch, built by `GraphiteLayer` each frame.

Draw the rest of the bar to the mockup, its lengths one to one
([0626](0626-size-fonts-by-the-em-the-mockup-was-drawn-at.md)):

- **Menus.** A top-level menu's fill is 22 px tall (`kMenuItemHeight`), centred
  in the 28 px bar, and reaches 8 px, the item spacing, past its label on each
  side, with corners of 4 px (`MenuItemRounding`), in the hover surface.
  - `Core::MenuBar::Render` takes `MenuBarHeights`: the row and the item.
  - ImGui stretches a menu's fill by half the vertical item spacing above and
    below its label, so the bar pushes the spacing that makes the fill the item's
    height. Each dropdown gets the window's own spacing back, with its padding.
  - `TitleMenuHeights` gives the custom title bar's pair. Under the system's
    title bar the menus keep ImGui's.
- **Window buttons.** Each is 42 px wide and as tall as the bar.
  - The glyphs are Lucide's at 14 px: `minus`; `square`, or `copy` on a
    maximised window, which reads as restore; and `x`.
  - They are drawn in the icon grey, the text colour 21 % of the way to the
    disabled text: `#c3c8cf` in the dark scheme.
  - Under the pointer a button fills with the hover surface, and close with
    `#c42b1c` while its glyph fades to white. A press shows the same fill
    ([0624](0624-show-a-press-in-the-hover-surface-and-read-the-accent-only-from-its-own-slot.md)).
  - `DrawWindowButtons` draws them and reports the press; `TitleBar::Render`
    acts on the window.
- **Logo.** It is 0.6 of the bar tall, 17 px at 28. On a light chrome
  (`Core::IsLightBackground` of the menu bar's background) its shades are drawn
  in the text colours instead of its own light greys. The darkest becomes the
  text colour and the lightest the disabled text; each shade between falls
  between them by its luminance, so the faces keep their order.

## Rationale

A window is told from another by its folder and branch, and the bar is the one
place that is always on screen. Taking the pieces as strings keeps the bar from
knowing about the workspace or Git, and building the branch with the status
bar's own function keeps one wording. Dropping whole pieces, as the version
label was dropped, keeps the bar from showing a name cut to something that
reads as another folder.

Deriving the icon grey, and the light logo, from the text colours gives the
light scheme the same relations as the dark one without values of its own.
Setting the menu fill through the item spacing keeps the menus ImGui's, with
their hover-to-switch and keyboard handling.

## Consequences

### Positive

- The window says what it has open without a panel being shown.
- The version label's fit rules are replaced by tested ones for three pieces.
- The menus, the window buttons and the logo have the mockup's sizes and
  colours, and the logo stays visible on a light chrome.

### Negative

- The version is no longer visible without opening About.
- A long folder name hides the branch even where a shortened name would let both fit.
- The branch is shown unabridged, with an operation such as MERGING, so it can
  be long enough to be the first piece dropped.
- What `drawLeading` and `drawTrailing` draw sees the bar's pushed item
  spacing. Both draw by position today, so neither depends on it.
- The light logo is derived, not drawn: its faces differ only by the span
  between the text and the disabled-text colours.

### Neutral

- The drag region and the hit test are unchanged.
- The pieces are display only: they take no clicks and show no tooltips.

## Alternatives Considered

- **Cut the folder name with an ellipsis.** It keeps both pieces longer, but
  `LayOutCaption` would measure text, and a cut name can read as another folder.
- **Show the branch in a button that opens the Git panel.** The status bar
  already does; a button in the drag region would need the hit test changed.
- **Draw the top-level menus as buttons of our own.** They could take any
  height, but the menu bar's hover-to-switch and keyboard handling would have
  to be written again.
- **Ship a second logo for light schemes.** It could be drawn by hand, but it
  is a second asset to keep in step with the first.

## Related

- [0477](0477-draw-the-title-bar-in-the-application.md): the drag region the
  pieces sit in.
- [0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md):
  the scaling every length goes through.
- [0624](0624-show-a-press-in-the-hover-surface-and-read-the-accent-only-from-its-own-slot.md):
  the hover surface and the missing pressed colour.
- [0625](0625-draw-the-interface-s-icons-from-lucide.md): the window buttons'
  glyphs.
- [0626](0626-size-fonts-by-the-em-the-mockup-was-drawn-at.md): the mockup's
  lengths taken one to one.
- [0630](0630-leave-the-branch-out-of-the-title-bar-since-the-status-bar-shows-it.md):
  takes the branch back out of the bar; the folder and the rest stand.
