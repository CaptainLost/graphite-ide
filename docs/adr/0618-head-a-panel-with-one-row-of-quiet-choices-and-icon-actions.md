# 0618. Head a panel with one row of quiet choices and icon actions

Status: proposed
Date: 2026-10-19

## Context

The Explorer opened with ImGui combo boxes, each as wide as the panel:

- the first chose the view, "File System" or "Visual Studio Solution";
- a second, in the solution view of a folder with several solutions, chose the
  solution.

Under them the file system view had its filter field with a Refresh button after
it. New File and New Folder were reachable only from the root's context menus.

ImGui's combo is framed and draws its current value and its arrow in one face.
Sized to its text, it still cannot draw the value in the strong face and the
arrow in the icon face, because the strong face carries no icons
([0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md)). Other
panels head themselves in their own ways, and none is asked to change now.

## Decision

Draw a panel's header row with `Graphite::DrawPanelHeader(choices, actions)`
(`Graphite/src/UI/PanelHeader.h`).

- **Row:** one row `PanelHeaderHeight()` tall, `kPanelHeaderHeight` (34 px)
  scaled. Every piece is centred vertically in it. Each piece is placed by
  position and the row is laid out once from its start, so what follows starts
  under the whole row.
- **Choices:** on the left, one or two quiet dropdowns (`PanelHeaderChoice`: an
  id, the options, the selected index), 2 px apart.
  - A dropdown is 24 px tall, with 6 px of padding at each end. It shows the
    current option's name in the strong face (`ImGuiRenderer::GetBoldFont()`) in
    the text colour, and 5 px after it `ICON_LC_CHEVRON_DOWN` in the interface
    face, in the disabled-text colour.
  - It has no frame. Under the pointer it takes a rounded fill in the hovered
    button colour, eased in as an icon button's is
    ([0602](0602-draw-every-button-that-is-only-a-glyph-through-core-drawiconbutton.md)),
    and the active button colour while held. While its popup is open it keeps the
    hovered fill.
  - A click opens a popup under it, at least as wide as it, listing the options
    with the current one selected. Choosing one reports its index in
    `Chosen[i]` on that frame, the current option too, and closes the popup.
- **Actions:** on the right, `PanelHeaderAction`s (an id, a glyph, a tooltip)
  drawn through `Core::DrawIconButton` in the `Frame` fit, 2 px apart, the last
  ending at the row's right edge. A click reports the action's index in
  `ClickedAction`.
- **Narrow rows:** the dropdowns share the room left of the actions, less 8 px.
  The narrowest takes its whole width first. A name that does not fit is cut
  with an ellipsis. A dropdown is never narrower than its padding and chevron.
- **The Explorer adopts it:**
  - The first dropdown chooses "File System" or "Visual Studio Solution". The
    choice is kept in the folder setting `explorerMode`, as the combo kept it.
  - The second shows only in the solution view of a folder with more than one
    solution. It lists their file names, and choosing one selects it through
    `Workspace::SelectSolution`, as the combo did.
  - The actions are the shown view's own:
    - the file system view has New File, New Folder and Refresh. New File and
      New Folder record the same actions as the root's context menu: a name field
      at the end of the root, and a folder named "new folder" in the root,
      renamed at once. Refresh is the button that stood after the filter:
      `Workspace::Reload` and a new background listing;
    - the solution view has New Folder, the solution root's context-menu item,
      which adds a solution folder and renames it, but only while the solution
      can be edited; and Refresh;
    - with no folder open there are no actions.
  - Their glyphs are `FILE_PLUS_OUTLINE`, `FOLDER_PLUS_OUTLINE` and `REFRESH`.
  - The two combo boxes and the button after the filter go. The filter field
    spans the full width in both views
    ([0601](0601-draw-every-field-that-narrows-a-list-through-drawfilterfield.md)).
- **Other panels** may adopt the header later. Only the Explorer does now.

## Rationale

One row gives the Explorer back two lines, puts the view's name where it reads
as the panel's title, and keeps the panel's actions together at the row's end.
Drawing the dropdown, rather than restyling ImGui's combo, lets it be only as
wide as its name, share its line with buttons, and draw the name and the chevron
each in the face that has it. Giving each view its own actions keeps a button
from acting on a view that is not shown.

## Consequences

### Positive

- The Explorer's tree starts two lines higher.
- New File and New Folder are one click away, not only in a context menu.
- A panel adopts the header with one call and two lists, with no layout of its
  own.

### Negative

- The dropdown does part of a combo's work itself: opening on a click, and the
  popup's place and width. A change to ImGui's combo does not reach it.
- A `.sln` solution shows no New Folder in the header, and nothing there says
  why. The root's context menu still shows the item disabled with the reason.
- The solution view's header has no New File, so its buttons differ from the
  file system view's.
- Refresh in the solution view rereads the whole folder, not only the solution.
- In a narrow Explorer showing two dropdowns, the names are cut.

### Neutral

- The separator under the header stays where it was under the combo.
- The popup lists the options with ImGui's `Selectable`, in the global header
  colours, as the combo's list did.
- In the tests the strong face is null and the chevron draws as ProggyClean's
  fallback glyph, so only the row's sizes, places and colours are observed.

## Alternatives Considered

- **ImGui's combo, unframed and sized to its text.** It draws its value and its
  arrow in one face, and its hover is the frame colour rather than the buttons'.
- **A segmented switch between the two views.** The mockups showed it beside the
  dropdown. It is wider, and the solution choice would need a dropdown anyway.
- **The same three actions in both views, acting on the folder.** In the
  solution view, New File's name field would open in the file tree, which is not
  drawn.

## Related

- [0601](0601-draw-every-field-that-narrows-a-list-through-drawfilterfield.md):
  the filter field, which now has no button after it in the Explorer.
- [0602](0602-draw-every-button-that-is-only-a-glyph-through-core-drawiconbutton.md):
  the header's buttons.
- [0608](0608-make-a-new-file-only-once-its-name-is-entered-and-offer-a-dialog-with-templates-beside-it.md):
  New File, which the header reaches as the root's menu does.
- [0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md): the
  strong face the dropdown's name is drawn in.
- [0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md):
  the scaling every length goes through.
