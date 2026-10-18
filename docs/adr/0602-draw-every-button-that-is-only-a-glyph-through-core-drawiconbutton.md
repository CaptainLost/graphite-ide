# 0602. Draw every button that is only a glyph through Core::DrawIconButton

Status: proposed
Date: 2026-10-16

## Context

A button that is only a glyph had six looks in the app.

- Some were transparent with a rounded fill under the pointer, as the toolbars' were.
- Some were filled at rest: the Explorer's refresh and the Changes rows' stage and discard buttons.
- The status bar's bell had no fill at all and only brightened its glyph.
- Some showed their tooltip at once, others after the pointer rested.

ImGui's own button pads its label with the frame padding. On a square as wide as a row, that padding leaves an inner box narrower than the glyph, so ImGui aligns the glyph left instead of centring it.

## Decision

Draw every button that is only a glyph with `Core::DrawIconButton` (`Core/src/Core/ImGui/IconButton.h`). There are two exceptions: the window's own buttons (minimise, maximise, close), and the tool strip's buttons, which `Core::DrawToolStrip` draws itself (0609).

- **At rest:** no fill and the glyph in the text colour.
- **Under the pointer:** a rounded fill in the hovered button colour, eased in.
- **Held:** the active button colour, but only while the pointer stays on the button.
- **Engaged:** a toggle that is on is filled and drawn in the accent.
- **Tooltip:** shown through `SetItemTooltip`, so it waits for the pointer to rest.
- **Two fits:**
  - `Frame` is a square a little taller than a line, centred in a row one frame high, for toolbars and bars.
  - `Line` is a square one text line high, for a list row, which must not grow taller for it.
- **Layout:** the whole cell is laid out and only the square inside it is the item. Whatever follows on the line keeps the line's top.
- **`DrawIconButtonFace`:** draws the same look for a button whose press is read elsewhere.

A hover that was not asked for on the frame before starts settled at its target (`Animation::HoverAmount`). This keeps a button that disappears and comes back, such as a row's button, from fading out from where it was.

## Rationale

**One look.** A glyph that can be clicked looks the same wherever it sits. The fill under the pointer shows what will be pressed, without crowding a toolbar or a list row at rest.

**Drawn, not ImGui's button.** Drawing the button ourselves centres the glyph exactly, and the two fits let it stand in a toolbar and in a list row alike.

## Consequences

### Positive

- Every glyph-only button looks and behaves alike, including its tooltip's delay.
- A list row with buttons stays one text line high.

### Negative

- The buttons are not keyboard navigation targets unless `ImGuiButtonFlags_EnableNav` is passed.
- The Explorer's refresh lost the fill that made it easy to spot.

### Neutral

- The status bar's bell no longer shows a hand cursor; its fill under the pointer is enough.

## Alternatives Considered

- **Filled at rest everywhere.** Easy to spot, but heavy and crowded in list rows and the status bar.
- **No fill, only a brighter glyph.** The lightest option, but the click target is just the glyph and is hard to hit.

## Related

- [0588](0588-draw-checkboxes-with-a-tick-box-of-the-app-s-own.md): another widget the app draws itself so that it looks the same everywhere.
- [0609](0609-toggle-tool-panels-from-a-strip-that-ignores-where-they-are-docked.md) — amends this record: the tool strip's buttons are a second exception, drawn by `Core::DrawToolStrip` with an open fill, a glyph dimmed while closed and a badge.
