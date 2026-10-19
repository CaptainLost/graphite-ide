# 0621. Space editor lines at 1.4 times the font size

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

- **Height:** an editor line is `round(1.4 × font size)` tall. The font size is
  ImGui's current font size, read once a frame after the code face is pushed at
  the editor's size, so it carries the zoom, the interface scale and the DPI. At
  100 % a 15 px line is 21 px tall, and a 13 px line 18 px.
- **Metrics:** `EditorMetrics` holds the font size, the line height and
  `TextOffsetY`, which is `(LineHeight − FontSize) / 2`, floored.
  `TextTopOf(metrics, rowTop)` is where text in a row is drawn, and
  `CentredTextOffset(lineHeight, textHeight)` centres text of any other size.
- **Text:** every run drawn at a row's top is drawn at `TextTopOf`: code, plain
  text, line numbers and lens labels.
- **Marks:** whatever marks a line spans the whole line: the current-line fill,
  the selection, find matches and the current match's outline, bracket boxes,
  indent guides, the gutter fill, change bars, the caret and a lens row's
  selection cell. Squiggles and underlines keep their place against the line's
  bottom, 2 px and 1 px above it.
- **Hit tests:** a pointer's row is `(y − top) / LineHeight`, floored, so a pixel
  above a line's top still belongs to the line above.
- **Hover:** the hover tooltip is anchored to the hovered token's whole line, so
  it opens below the line rather than over its lower part. The tooltip's own code
  rows stay one text line tall: the overload note is laid beside a declaration's
  row with `SameLine`, at the row's top, and would stand above code centred in a
  taller row.
- **Elsewhere:** the diff view, the search preview, the console and the change
  tooltip keep their spacing.

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

- A view shows about 29 % fewer lines, and Page Up and Page Down step by fewer.
- The editor and the diff view and search preview now step at different pitches,
  so a line of code is taller in the editor than in a diff of the same file.
- The tooltip's code rows and the editor's rows differ in pitch too.
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
