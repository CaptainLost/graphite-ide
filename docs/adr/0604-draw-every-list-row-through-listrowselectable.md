# 0604. Draw every list row through ListRowSelectable

Status: proposed
Date: 2026-10-16

## Context

The app's list rows came in three heights:

- a line plus 5 px in most lists;
- a line plus 8 px in the review;
- 28 px in the settings navigation.

Some tables, such as the requests and the key bindings, stood a pixel shorter than the rest.

A row's highlight reached from its indent in some lists and from the list's edge in others. The highlights of tree folders left 5 px gaps between rows; file rows' highlights touched. A row's end, such as a date, a count or a status letter, stood 0, 8 or 16 px from the edge, and was dim in some lists and not in others.

## Decision

Draw every list row through `Graphite/src/UI/Lists/ListRow.h`.

- **Height:** a row is one text line plus the item spacing, `ListRowPitch`.
- **Highlight:** `ListRowSelectable` is a Selectable from the list's left edge to its right, whatever the row's indent, one text line high. Its highlight is square and fills the whole pitch, so the highlights of adjacent rows touch.
- **Tables:** a table's rows are list rows when its cell padding is `ListTableCellPadding`, which is half the item spacing above and below.
- **List edges:** a list is drawn in a child with no padding at its sides, so its edges are the list's own. Rows that need an inset at both ends indent their content by `ListRowEndInset`.
- **Row end:** whatever stands at a row's end stands at `ListRowEnd`, which is 8 px from the list's visible right edge and clear of a scrollbar.
- **Colours:** dates and counts are drawn in the disabled-text colour; status letters are drawn in their status colour.
- **Selected and hovered:** a selected row rests in the header colour, and a row under the pointer takes the hovered header colour. The settings navigation keeps its own height and rounding, but uses these colours.

## Rationale

**One rhythm.** Lists one beside the other read alike, and a highlight that fills the row edge to edge shows plainly which row the pointer is on.

## Consequences

### Positive

- Every list has the same row height, the same highlight and the same margin at its end.
- A tree's folder rows and file rows highlight alike, with no gaps between them.

### Negative

- The review's rows are a little shorter than they were.
- The review's tick boxes sit in a row one line high, with the frame padding removed so that they fit.
- Changes lost its count pill; its count is now dim text after the heading.

### Neutral

- The notification history keeps its own taller rounded rows, as cards in a popup.

## Alternatives Considered

- **Rounded, inset highlights.** In tables the rounding cuts across the columns.
- **Keep each list as it is, sharing only the code.** The four heights and three margins would stay.

## Related

- [0603](0603-mark-whatever-opens-and-closes-in-a-list-with-the-one-dim-chevron.md): the tree row built on the list row.
