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

## Decision

Draw, after the menus, the open folder and its branch instead of the version.

- **Pieces, in order:** a 1 px divider in the border colour; the folder glyph
  `ICON_MDI_FOLDER_OUTLINE` in the disabled-text colour and the folder's name in
  the text colour; a gap; `ICON_MDI_SOURCE_BRANCH` in the disabled-text colour
  and the branch in the text colour. A glyph and its text are 6 px apart. The
  divider is half the bar tall; every piece is centred vertically in the bar.
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

## Rationale

A window is told from another by its folder and branch, and the bar is the one
place that is always on screen. Taking the pieces as strings keeps the bar from
knowing about the workspace or Git, and building the branch with the status
bar's own function keeps one wording. Dropping whole pieces, as the version
label was dropped, keeps the bar from showing a name cut to something that
reads as another folder.

## Consequences

### Positive

- The window says what it has open without a panel being shown.
- The version label's fit rules are replaced by tested ones for three pieces.

### Negative

- The version is no longer visible without opening About.
- A long folder name hides the branch even where a shortened name would let both fit.
- The branch is shown unabridged, with an operation such as MERGING, so it can
  be long enough to be the first piece dropped.

### Neutral

- The drag region and the hit test are unchanged.
- The pieces are display only: they take no clicks and show no tooltips.

## Alternatives Considered

- **Cut the folder name with an ellipsis.** It keeps both pieces longer, but
  `LayOutCaption` would measure text, and a cut name can read as another folder.
- **Show the branch in a button that opens the Git panel.** The status bar
  already does; a button in the drag region would need the hit test changed.

## Related

- [0477](0477-draw-the-title-bar-in-the-application.md): the drag region the
  pieces sit in.
- [0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md):
  the scaling every length goes through.
