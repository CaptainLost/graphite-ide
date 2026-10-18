# 0616. Draw list rows taller, with an inset rounded highlight in the accent

Status: proposed
Date: 2026-10-18

## Context

Under [0604](0604-draw-every-list-row-through-listrowselectable.md) a list row
is one text line plus the item spacing, 19 px with a 14 px interface line. Its
highlight is square and runs from edge to edge in the global `Header` and
`HeaderHovered` colours, two greys a step apart, so a selected row reads as a
hovered one. Menus and combo boxes draw in the same `Header` colours, so
recolouring them globally would recolour those too.

ImGui's `Selectable` fills its whole box square, and that box is also what the
pointer hits. Callers lay a row's pieces after the Selectable, by position or
with `SameLine`, and ImGui ends each line at its tallest piece plus the global
item spacing, so a row is only as tall as its callers' line unless the row
itself holds that line taller. A list drawn in a child without padding starts at
the child's top edge, so anything drawn above a row's start is cut off.

## Decision

Draw list rows through `Graphite/src/UI/Lists/ListRow.h` as follows.

- **Pitch:** a row is one text line plus `ListRowSpacing()`, 9 px scaled:
  `ListRowPitch()`, 23 px with a 14 px line. The global `ItemSpacing` stays as
  it is.
- **Slot:** a row owns the space from the cursor to a pitch below it.
  `ListRowSelectable` lays out a Selectable a pitch high with no item spacing,
  so its box is the slot: the first row's highlight starts at the list's top,
  and the list's content reaches the bottom of the last row.
- **Text:** a row's text stands at `ListRowTextTop(rowTop)`, half the row
  spacing below the row's top, floored, so it sits in the middle of the row.
  Every piece a caller places on a row by position is placed there. Text laid
  with `SameLine` after the row lands there on its own, because the row leaves
  that offset as its line's text baseline.
- **Line:** the row holds its line so that whatever is laid on it, by
  `SameLine` or at the text top, closes the row at the pitch. A row laid out by
  hand that ends on its own `Dummy` or `ItemSize` makes it `ListRowLineHeight()`
  tall, the pitch less the item spacing, from the row's top. A field that stands
  in a row's place, such as the Explorer's rename and new-file field, does the
  same, with its frame padding equal to the text offset so that its text is on
  the rows' text line.
- **Highlight:** inset 6 px from the list's two edges, rounded 5 px, and as tall
  as the slot, so the highlights of adjacent rows still touch. The keyboard's
  mark is drawn round the inset highlight. The row's click target stays the
  list's full width.
- **Colours:** derived from the current style each time a row is drawn, through
  `CurrentListRowColours()`:
  - selected: the accent (`ImGuiCol_CheckMark`) at alpha 0.20;
  - hovered: the window background mixed 6 % toward the text colour;
  - pressed: the accent at alpha 0.30.
- **Order:** a row held down under the pointer shows the press. Otherwise a
  selected row shows the accent, under the pointer or not. Otherwise a row under
  the pointer shows the hover. This is not `Selectable`'s own order, which lets
  the hover cover a selected row.
- **Overlap:** a row asked to allow overlap gives that to its item as well as to
  its Selectable, so it lets go of its hover while an item laid over it, such as
  a row's button, is under the pointer.
- **Colours stay per row:** `ListRowSelectable` clears `Header`,
  `HeaderHovered`, `HeaderActive` and `NavCursor` around its Selectable, pops
  them before it returns and draws the fill and the mark itself. The global
  header colours, which menus and combo boxes draw in, do not change. The order
  lives in `ListRowHighlight(selected, hovered, held)`, which the list rows, the
  table rows and the settings navigation share.
- **Settings navigation:** an entry keeps its 28 px height and 5 px rounding and
  takes the list colours in their order. Its hover still fades in and out.
- **Tables:** a table whose rows are list rows takes the following.
  - *Pitch:* its cell padding is `ListTableCellPadding`, half the row spacing
    above and below, and each row is given the pitch as its least height, so a
    row of one text line is a pitch tall. The history graph is placed by the
    same pitch.
  - *Ends:* `PadListTableEnds`, called straight after `BeginTable`, pads the
    table's two ends by `ListRowEndInset`, so the first and last cells stand
    inside the highlight.
  - *Row:* a row is picked by `TableRowSelectable`, a Selectable laid out from
    the row's top, a pitch high and across every column, so its box is the
    row's whole slot. Its own fill and keyboard mark are cleared, and it puts
    the cell back as one holding a text line, so what follows with `SameLine`
    stands at the cell's text.
  - *Highlight:* one shape per row, drawn in the table's background channel
    under every column. It is inset 6 px from the table's two ends and rounded
    5 px there only, so the rounding never cuts across a column. It is drawn in
    the list colours and their order, with the keyboard's mark round it.
  - *Rows without a Selectable:* such a row, as a key binding's is, calls
    `DrawTableRowHighlight(selected, hovered)` once its cells are drawn. It is
    highlighted from its top to its bottom however many lines it holds. A key
    binding's row is hovered when ImGui reports the pointer over it; its slots
    keep their keyboard mark but not their own fill.
  - *Stripes:* such a table draws no alternating row background, which the
    hover colour would all but match.

## Rationale

A taller row and a selection tinted with the accent tell a selected row from a
hovered one at a glance, and an inset rounded highlight reads as one item in a
list. The hover is a quieter lift than the accent, so a selected row keeps the
accent under the pointer, or it would read as deselected. A row that owns its
whole slot cannot be cut at a list's edges, and drawing the fill in the row keeps
the click target the list's full width and leaves every other `Header` user as
it was.

## Consequences

### Positive

- A selected row no longer looks like a hovered one, with or without the
  pointer on it.
- Menus and combo boxes keep their colours.
- Every list and tree, plain or laid out by hand, steps by the same pitch, with
  its text in the middle of the highlight.
- The first and the last row of a list show their whole highlight.
- A table's row is one highlight across its columns, with no gap above or below
  it, and the pointer moving down a table never falls between two rows.

### Negative

- Lists are about a fifth taller, so fewer rows fit on screen.
- Every piece a caller places on a row by position has to be placed at
  `ListRowTextTop`; one placed at the row's top stands above the text line.
- The row mirrors `Selectable`'s state through `IsItemHovered` and
  `IsItemActive`. A change in how ImGui shows a Selectable's state does not
  reach list rows on its own.
- The row's line is held in ImGui's per-window line state. After a row nothing
  is laid over, the next item that is not a row takes a line at least a text
  line tall.
- `TableRowSelectable` and `PadListTableEnds` set ImGui's table and cursor state
  directly: the table's outer padding and the cell's line. A change in how ImGui
  lays out a table's cells does not reach them on its own.
- The tables that take the row look lose their alternating row background.
- A key binding row's hover follows the pointer a frame late, as ImGui reports
  the row under the pointer only from the frame before.
- The hover lift follows the text colour current at the row, so a row drawn in
  a dimmed text colour, such as an ignored file, lifts a little less.

### Neutral

- The notification history keeps its cards.
- The cost view's open turn takes the accent at a fifth, as any selected row
  does, in place of its own stronger wash.
- The first and last cells of a list table stand 8 px from the table's ends,
  where they stood at the cell padding or flush before.
- A search hit row, drawn in the code face, is a 15 px line plus the row
  spacing: 24 px.
- At large scales the text top is held a pixel above the middle, so that a text
  line laid there still ends inside the row's line.

## Alternatives Considered

- **Pad a text-line-high Selectable by half the row spacing above the row's
  start and the rest below.** The text needed no offset of its own, but the first
  row's highlight was cut at the top of a list without padding, and the last
  row's reached past the list's content.
- **A Selectable over the inset rectangle, rounded through
  `SelectableRounding`.** ImGui would draw the fill and its states itself, but
  the click target would shrink to the inset, leaving 6 px at each edge that
  hits nothing, and the row's right end would need the work rect changed for the
  call.
- **Set the header colours globally.** Menus and combo boxes would take the
  accent tint and the quiet hover.
- **Push the row spacing as `ItemSpacing` around each list.** Every caller would
  change, and the items in a list that are not rows would take the row spacing
  too.
- **ImGui's own row background, through `TableSetBgColor`.** ImGui fills it
  square from one end of the table to the other, with no inset or rounding.
- **A highlight per cell.** The rounding would cut across every column.
- **A square highlight across the row.** It would fill the row and span every
  column, but a table's rows would not read as a list's.

## Related

- [0604](0604-draw-every-list-row-through-listrowselectable.md): amended by this
  record.
- [0603](0603-mark-whatever-opens-and-closes-in-a-list-with-the-one-dim-chevron.md):
  the tree row built on the list row.
- [0613](0613-draw-the-chrome-darker-than-the-documents-and-diffs-it-frames.md):
  the chrome the hover lifts from.
