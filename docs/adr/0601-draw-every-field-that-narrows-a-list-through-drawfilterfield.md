# 0601. Draw every field that narrows a list through DrawFilterField

Status: proposed
Date: 2026-10-16

## Context

Nine panels have a field that narrows the list under it: the Explorer, the solution tree, the branches and their authors, the history (by words and by path), the review's files, the sessions, the key bindings, the console and the settings search. Each was put together on its own.

- Some showed a magnifier in the hint, so it vanished as soon as anything was typed; the settings search kept it inside the field.
- Only two had a button to empty the field, one inside the field's line and one beside it.
- Only two emptied on Esc; the rest left Esc to undo the typing.
- The Explorer put its refresh button before the field, the branches put their options button after it.

ImGui's text field cannot hold a button inside its frame or keep a glyph at its start, and it fills its frame with the frame colour whether it is hovered or typed in.

## Decision

Draw every field that narrows a list with `Graphite::DrawFilterField` (`Graphite/src/UI/Lists/FilterField.h`).

- The field draws one frame across its whole width and gives the text field inside it a clear frame, so whatever sits inside reads as part of one field.
- A glyph in the disabled-text colour stays at the field's start, typed text or not: the magnifier for words, another glyph where the field takes something else, as the history's path field takes a path. The hint and the text start after it, and what InputText draws there once a long query scrolls is clipped.
- Once there is text, a clear button sits inside the field's right end, if the field has room for it. Esc empties the field too.
- The panel's own buttons go after the field, outside it. `FilterFieldWidthBefore(n)` gives the field the line less room for `n` icon buttons.
- The width is floored, as ImGui lays out an item's, and never less than a pixel.

A search that runs on Enter (Find in Files, the find bar) is not a filter and keeps its own field.

## Rationale

**One recipe.** A field that narrows a list should say so the same way everywhere, and empty the same way. The glyph stays because the hint goes when typing starts, and with it the only sign that the field is a filter.

**The button inside, the panel's buttons outside.** The clear button belongs to the field; refresh and options belong to the panel, and always stand at the same place, at the line's end.

## Consequences

### Positive

- Every filter field looks and behaves alike: glyph, hint, clear button, Esc.
- A panel adds a filter in one call, its buttons in one more.

### Negative

- The field mirrors part of ImGui's InputText layout: the frame colour, the text's start after the frame padding. A change to how ImGui draws its text field can make the two drift apart.
- With text, the keyboard navigation highlight goes round the text part of the field only, not the clear button.
- Esc now empties fields that used to only undo the typing; in the branch list's author popup, closing it with text in the field takes one Esc more.

### Neutral

- The settings search has the six-pixel corners every field has since [0627](0627-draw-fields-as-the-mockup-s-bordered-faint-filled-boxes.md), so it pushes no rounding of its own. The magnifier is drawn at twelve pixels, eight in from the field's start, and the text starts six after it.

## Alternatives Considered

- **A clear button beside the field.** Simpler, but it reads as a panel button and moves whatever comes after the field each time text appears.
- **The magnifier in the hint.** Gone once anything is typed, so a field with text looks like any other text field.
- **Overlay the button on ImGui's own frame.** The text runs under the button once it is long enough, and the caret with it.

## Related

- [0588](0588-draw-checkboxes-with-a-tick-box-of-the-app-s-own.md): another widget the app draws itself for one look everywhere.
