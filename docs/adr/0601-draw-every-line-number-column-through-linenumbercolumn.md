# 0601. Draw every line number column through LineNumberColumn

Status: proposed
Date: 2026-10-16

## Context

Three views show a file's lines with their numbers: the editor, the search preview, and the diff. Each one sized and drew its numbers in its own way.

- **The editor** had three digits at least, sized for the file's last line, measured in the font's cells, with 8 px on each side. The numbers were dim, and the caret's line was brighter.
- **The search preview** sized the column for the last line of the stretch it showed, so its width changed from one hit to the next. It left one cell after the numbers.
- **The diff** always used five digits, measured with `CalcTextSize`, which rounds each advance up. Its numbers were in the ordinary text colour.

## Decision

Draw every line number column with `Graphite::LineNumberColumn` (`Graphite/src/UI/LineNumberColumn.h`).

- **Width:** as many digits as the file's last line has, three at least, measured in the baked font's cell width, with a gap of 8 px on each side.
- **Placement:** a number stands right-aligned, a gap short of the column's right edge.
- **Colour:** numbers are drawn in the scheme's line number colour. The line that matters is brighter: the caret's line in the editor, the hit's line in the preview. The diff has no such line yet.
- **The diff's line count:** the diff takes its last line from the file's two texts, or from the hunks when the texts are not at hand.

## Rationale

**One column.** A number column that looks the same in the editor, the preview and the diff lets the eye move between them without adjusting. Sizing it for the file keeps it from changing width while the view scrolls.

## Consequences

### Positive

- The three views agree on the column's width, its gap and its colour.
- The diff's column fits the file, and its numbers recede behind the code.

### Negative

- The preview's column is a few cells wider than it was, so in a narrow preview the code starts further right.
- A commit diff can widen its column by one digit once, when the files' texts arrive after the hunks.

### Neutral

- The editor's column looks as it did; it is the one the other two views were brought to.

## Related

- [0599](0599-draw-every-list-row-through-listrowselectable.md): the same push towards one look per kind of thing.
