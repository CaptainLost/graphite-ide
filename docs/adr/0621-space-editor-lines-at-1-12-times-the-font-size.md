# 0621. Space editor lines at 1.12 times the font size

Status: proposed
Date: 2026-10-19

## Context

An editor line was as tall as the font: `MeasureEditor` took its height from
`ImGui::GetTextLineHeight()`, which is the font size. Lines stood edge to edge,
and the current-line fill, the selection and the caret hugged the glyphs. ImGui
draws a run of text from its top, so text drawn at a row's top leaves any extra
height below it. Every conversion between rows and pixels in the editor already
goes through one figure, `EditorMetrics::LineHeight`: drawing, clicks, hover,
the list clipper, scrolling to the caret, the scroll anchor, the page step and
the scrollbar's change marks. A lens row is one line tall
([0486](0486-insert-non-document-rows-above-rowmap-rather-than-inside-it.md)).
Zoom changes the editor's font size, and the interface scale and the monitor's
DPI scale it again
([0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md)).

## Decision

- **Height:** an editor line is `round(1.12 × font size)` tall. The font size is
  ImGui's current font size, read once a frame after the code face is pushed at
  the editor's size, so it carries the zoom, the interface scale and the DPI. At
  100 % a 17 px line, the default size, is 19 px tall, which is the mockup's
  line at its 13 px code, the em JetBrains Mono draws at 17 px
  ([0626](0626-size-fonts-by-the-em-the-mockup-was-drawn-at.md)). A 20 px line
  is 22 px tall.
- **Metrics:** `EditorMetrics` holds the font size, the line height and
  `TextOffsetY`, which is `(LineHeight − FontSize) / 2`, floored.
  `TextTopOf(metrics, rowTop)` is where text in a row is drawn,
  `TextBottomOf(metrics, rowTop)` is that top plus the font size, the foot of
  the text's box, and `CentredTextOffset(lineHeight, textHeight)` centres text
  of any other size.
- **Text:** every run drawn at a row's top is drawn at `TextTopOf`: code, plain
  text, line numbers and lens labels.
- **Marks:** whatever marks a line spans the whole line: the current-line fill,
  the selection, find matches and the current match's outline, bracket boxes,
  indent guides, the gutter fill, change bars, the caret and a lens row's
  selection cell.
- **Squiggles and underlines:** these belong to the text, not the line, so they
  are measured from `TextBottomOf`. A diagnostic squiggle's middle stands 2 px
  above it, and the Ctrl-hover link underline and a hovered lens label's
  underline 1 px above it, both through `Core::Scaled`. They keep the place
  against the glyphs that they had when a line was as tall as its text, at every
  zoom.
- **Hit tests:** a pointer's row is `(y − top) / LineHeight`, floored, so a pixel
  above a line's top still belongs to the line above.
- **Hover:** the hover tooltip is anchored to the hovered token's whole line, so
  it opens below the line rather than over its lower part. The tooltip's own code
  rows stay one text line tall: the overload note is laid beside a declaration's
  row with `SameLine`, at the row's top, and would stand above code centred in a
  taller row.
- **Elsewhere:** the diff view, the search preview, the console and the change
  tooltip keep their spacing.

### Decision, part two: colours and lens labels

The room between lines lets the gutter and the current line read as bands, so
the marks around the code are dimmed to keep the code in front.

- **Line numbers:** dark `#4D525B`, light `#B4B4B4`. The caret's line number
  stays in the text colour.
- **Current line:** dark white and light black, both at alpha 10 of 255 (0.04).
  The fill spans the gutter as well, and the padding of the window that holds
  the editor surface: it starts at that window's left edge
  (`ImGuiWindow::Pos.x`), the editor's edge, not at the surface's, and covers
  the full row. The surface clips its drawing at its own left edge, so the fill
  is drawn under the surface's clip rectangle widened to the window's edge; the
  clip keeps the surface's top and bottom, so the fill never reaches over the
  breadcrumb or the horizontal scrollbar. The gutter fill is drawn after the
  row's text and is opaque, so the highlight is drawn again over the gutter
  alone, before the breakpoint, change, number and fold columns, which stay on
  top.
- **Indent guides:** half the alpha they had. Dark goes from 89 to 45, light from
  40 to 20.
- **Lens labels:** the interface face at 0.78 times the editor's font size, in
  `EditorColor::Lens`, dark `#6B7180`, light `#A0A0A0`: 13 px at the default
  17 px, the mockup's 11 px converted by Inter's factor (0626). The size follows
  the zoom. A label is centred in its row at its own size, is hit across the width
  it is measured at in that face and size, and its hover underline stands 1 px
  above the label's own bottom, not the code text's.
- **User schemes:** a scheme that overrides any of these keys keeps its value;
  only the built-in defaults change.

## Rationale

Room between lines makes code easier to read, and a fill or a selection with room
round the glyphs reads as a band rather than a box drawn tight to the text.
Centring puts the room evenly above and below. Taking the height from the font
size in effect keeps the proportion at every zoom with no setting of its own, and
keeping one figure for every conversion means a click, the caret and the drawing
cannot disagree about where a line is.

## Consequences

### Positive

- Lines no longer touch, and the current line, the selection and find matches
  read as bands across the line.
- Zooming keeps the same proportion between text and line.
- A click lands on the line drawn under the pointer, at every zoom.

### Negative

- A view shows about 11 % fewer lines than with lines as tall as their text, and
  Page Up and Page Down step by fewer.
- The search preview and the hover tooltip's code rows step at the bare text
  line, so a line of code there is shorter than in the editor: 17 px against
  19 px at 17 px. The diff view, the console and the change tooltip step at a
  text line plus their spacing. The diff view's 22 px at 17 px is 3 px taller
  than an editor line.
- When the room left is odd, the text stands half a pixel above the middle.

### Neutral

- The scroll anchor, the page step, the clipper and the scrollbar's marks take
  the new height with no change of their own, being handed the figure.
- The caret's screen position, which popups opened from the keyboard are placed
  at, is still the top of its line.

## Alternatives Considered

- **A fixed padding in pixels, such as 6 px.** It does not follow the zoom: at a
  large size lines look cramped again, and at a small one far apart.
- **Text at the row's top with the room below it.** The fill, the selection and
  the caret would hang below the glyphs.
- **Squiggles and underlines measured from the line's bottom.** They stood
  below the glyphs by the room a taller row adds under the text, and further
  still at a larger zoom, so they read as marks in the gap between lines, not under the
  text.
- **A user setting for the factor.** Nobody asked for one, and it can be added
  over the factor later without changing anything this record decides.
- **The diff view, the search preview and the console at the same pitch.** They
  lay their rows out by rules of their own, and their spacing is a separate
  decision.

## Related

- [0486](0486-insert-non-document-rows-above-rowmap-rather-than-inside-it.md):
  lens rows, which take the new height.
- [0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md):
  the scales the font size carries.
- [0606](0606-draw-every-line-number-column-through-linenumbercolumn.md): the
  line number column, whose numbers are centred in the line.
- [0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md): the
  code face the line is measured from.
