# 0588. Draw checkboxes with a tick box of the app's own

Status: proposed
Date: 2026-10-14

## Context

ImGui's checkbox fills its box with the frame colour and draws its tick on top. In the dark scheme an unticked box is a dark square that nearly disappears on the background, and a ticked one reads as a stray block of colour.

Style colours cannot change its shape. The only outline ImGui offers is `FrameBorderSize`, and that outlines every frame in the app at once: text fields, combos and buttons alike.

The app had fourteen checkboxes, in panels, toolbars and dialogs, plus the branch review's viewed mark on every file row.

## Decision

Draw every checkbox with `Graphite::DrawTickBox` (`Graphite/src/UI/TickBox.h`), never with `ImGui::Checkbox`.

- Unticked: a thin outline in the surface-active colour, nothing inside. Hovering eases the outline lighter.
- Ticked: the outline in the accent, the inside lightly tinted with it, and an accent tick drawn as a line. The change between the two is eased.
- Every colour comes from the style the active scheme set, so the light scheme follows on its own.
- A second form takes an outline colour for a box that has something to say about what it marks. The review outlines a file changed since it was viewed in the colour its "changed" word uses.
- `TickBoxWidth` gives the width for a caller that places a tick box against a right edge.
- `scripts/Test-NoImGuiCheckbox.ps1` fails on any `ImGui::Checkbox` in `Core/src` or `Graphite/src`.

It lives in Graphite's `UI` folder rather than in Core: the look is this application's, as the icon buttons' is.

## Rationale

**A quiet box.** The box is there to be scanned, not to draw the eye; an outline marks the place, and only a ticked box carries colour.

**One widget, not a restyle.** ImGui's checkbox cannot take this look without changing every other frame, so the app draws its own, and a check keeps the old one from coming back by habit.

## Consequences

### Positive

- Every checkbox in the app looks the same and can be seen when unticked.
- The review can mark a box with a colour of its own without a second widget.

### Negative

- One more widget to keep in step with ImGui's behaviour: label clicks, keyboard focus, the disabled look, the text baseline after it.
- `Core::Toggle` and the tick box are drawn by two different pieces of code with similar animation.

### Neutral

- Settings keep their switch, `Core::Toggle`.

## Alternatives Considered

- **Restyle ImGui's checkbox through colours.** Its frame fill stays; an unticked box would still be a filled square.
- **Turn on `FrameBorderSize`.** Outlines every text field, combo and button too.
- **Use the switch everywhere.** Right for settings, too heavy for a column of file rows and for toolbar options.

## Related

- [0587](0587-show-a-changed-file-s-diff-since-the-version-that-was-viewed.md): the "changed since viewed" state the review's outline shows.
- [0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md): the review whose file rows carry the viewed mark.
