# 0648. Let a tab's view put its tools on the right of the path row, and give the row to image tabs

Status: proposed
Date: 2026-10-26

## Context

- [0623](0623-show-the-file-s-path-and-the-symbol-at-the-caret-above-the-code.md)
  made the path row display only: one invisible button that never acts spans
  the whole row, so a press there focuses nothing. It also said a tab with no
  text document has no row, and that every image opens as such a tab.
- An image tab needs controls near its picture: zoom buttons and a switch for
  the background behind the picture. The row above the picture is where the
  eye already looks for what the tab is, and it is free on its right.
- A control drawn over the row's button would never see a press, since the
  button is laid out first and spans the row.

## Decision

- **Tools on the right.** `DrawBreadcrumb(model, tools)` takes a
  `BreadcrumbTools`: a `Width` in screen pixels, measured by the caller, and a
  `Draw` callback. `Draw` runs with the cursor at
  `(row.Max.x - kBreadcrumbRightPadding - Width, row.Min.y)`, inside the row's
  clip rectangle. With no tools the row is drawn as before.
- **Room.** The folders fit into the row's width less both paddings and
  `Width`, so the leading folders go before the tools are crowded.
- **Clicks.** The never-acting button covers only the part of the row left of
  the tools, so the tools take clicks and the rest of the row stays inert. After
  the row the cursor stands at its bottom edge whatever the tools did with it.
- **Image tabs get the row.** An image tab is drawn with the row above its
  picture, as a text tab is, and its view puts its tools on the right. This
  amends 0623's statement that every image opens as a tab with no row.

## Rationale

The row is already the tab's second line and already stands at a fixed height
under the tab bar, so tools there cost the picture no more room than the row
does. Letting the caller draw its own widgets keeps the row ignorant of what
they are, and leaving the rest of the button in place keeps 0623's reason for it:
a press on the row's empty part must not take the keyboard from the editor.

## Consequences

### Positive

- An image tab has a place for its controls that moves nothing when they change.
- Other views can put tools there without another layout path.

### Negative

- The row is no longer purely display-only: its right end can act.
- A caller that measures its tools wrongly overlaps the folders or leaves a gap;
  the row trusts `Width`.

### Neutral

- A tab whose file could not be read still has no row.

## Alternatives Considered

### Draw the tools in the picture's own area

Rejected: they would cover the picture, and the picture's pan and zoom would
need to know where not to react.

### Put the tools in the status bar

Rejected: 0620 and 0647 give the status bar items about the document, not
controls over it, and the bar is far from the picture.

## Related

- [0623](0623-show-the-file-s-path-and-the-symbol-at-the-caret-above-the-code.md) —
  the row; amended here.
- [0647](0647-let-the-view-on-screen-add-its-own-items-to-the-status-bar.md) —
  the status bar's items from the view on screen.
- `Graphite/src/Editor/Breadcrumb.h` — `BreadcrumbTools` and `DrawBreadcrumb`.
