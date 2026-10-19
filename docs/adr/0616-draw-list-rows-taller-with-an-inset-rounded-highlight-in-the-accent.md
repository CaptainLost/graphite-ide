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

A panel's list is a child of the panel's window, inside the window's 8 px
padding, so an inset measured from the list stands 14 px from the panel's edges.
ImGui keeps a window's content clear of its scrollbar's lane and draws the bar
before the content, so no row can be drawn under the bar. ImGui puts a tree
node's arrow a frame padding into the row and its label a font size and two
frame paddings in, 32 px with the 16 px interface face.

## Decision

Draw list rows through `Graphite/src/UI/Lists/ListRow.h` as follows.

- **Pitch:** a row is one text line plus `ListRowSpacing()`, 6 px scaled:
  `ListRowPitch()`, 22 px with the 16 px interface line, the mockup's row
  ([0626](0626-size-fonts-by-the-em-the-mockup-was-drawn-at.md)). The global
  `ItemSpacing` stays as it is; a list body caps it for its rows (below).
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
- **List body:** ImGui ends a line at its tallest piece plus the item spacing,
  so a row's text, centred and laid on the row's line, ends the row below its
  pitch while the item spacing is more than half the row spacing: the
  interface's 5 px against 3 px.
  - Every list lays its rows out inside a `ListBodyScope`, made inside the
    list's window and ended before that window. It caps `ItemSpacing.y` at
    half the row spacing for whatever is laid out inside it, and gives the
    interface's spacing back when it ends. Forms, menus and dialogs outside
    lists keep 5 px.
  - The list bodies are the Explorer's tree, the solution view, Changes,
    Problems, the hits of Find in Files and References, the sessions, a
    request's items, the branches and stashes, and the review's file tree and
    commits.
  - A popup begun inside a list body, such as a row's context menu or a row's
    tooltip of several lines, is begun inside a `ListPopupScope`. It pushes the
    spacing that the earliest push of the item spacing replaced, which is the
    interface's, until the popup has ended.
  - A row laid out outside a list body, where the item spacing is more than half
    the row spacing, is reported by `ListRowSelectable` as an ImGui error: a
    debug build asserts, and a release build logs it once. A test lays its rows
    inside a list body, as the panels do.
  - `ListRowTextTop` still holds the text higher outside a list body, so a row
    laid there in a release build keeps its pitch.
- **Highlight:** inset 6 px from the list's two edges, rounded 5 px, and as tall
  as the slot, so the highlights of adjacent rows still touch. The keyboard's
  mark is drawn round the inset highlight. The row's click target stays the
  list's full width.
  - A list is as wide as its panel (below), so the highlight stands 6 px in from
    the panel's edges.
  - While the list shows a scrollbar, the right inset is measured from the
    scrollbar's lane: 6 px plus the 12 px lane.
- **Panels:** a tool panel whose body is a list is flush
  (`Core::Panel::Sides::Flush`). The panel manager begins its window with no
  padding at its sides and the interface's padding above and below.
  - The flush panels are the Explorer, Changes, Problems, Find in Files,
    References and Claude Sessions. The Git view, whose lists stand in a table's
    columns, the console and Settings stay padded.
  - Whatever a flush panel draws that is not a list body is drawn inside a
    `Core::PanelMarginScope`. These are the panel header and the filter field,
    the toolbars and summary lines of Find in Files, References and Claude
    Sessions, the commit box of Changes, the empty and loading states, and the
    divider and preview under a split list.
  - The scope stands what it holds the interface's window padding, 8 px, in
    from the sides of a flush panel's window, docked or floating. It changes
    nothing in a padded window, or inside a child window such as a list's,
    whose content reaches its edges on purpose. ImGui flags a docked window as a
    child of its dock node's host too, so a window counts as a child only while
    it is not docked.
  - A scope narrows the window's work rect and content region and indents it,
    so it must not straddle a group or a table, which put back at their end what
    they began with. Scopes do not nest: one inside another adds the margin
    again.
  - Text a list lays where its rows go is inset as the rows' content is.
    Problems indents its whole list by a row end's inset, 8 px, as the Git
    view's list of branches does, so its file headings stand off the panel's
    edge. A tree's "No files match" message starts where a row's chevron does
    and wraps at a row's end (`DrawTreeMessage`).
  - The padding is pushed for the window's own `Begin` only. Its dock tab is
    drawn by the dock node and stays where it stood, as do the tabs beside it.
    A popup or a child begun inside the window takes the interface's padding
    again.
  - No separator line stands between a panel's header, filter or toolbar and
    its list, or above the commit box of Changes; the interface's item spacing
    does.
- **Tree row:** a tree node's chevron stands centred in a 14 px box 12 px into
  the row (`TreeRowChevronBoxLeft`, `TreeRowChevronBoxWidth`), 6 px past the
  highlight's inset, and a file's open-tab mark stands in the same box.
  - The label, the icon and the name after it, starts 5 px after the box:
    `TreeRowLabelOffset()`, 31 px.
  - With the icon's 16 px slot and its 5 px gap
    ([0617](0617-draw-file-icons-in-a-colour-by-file-kind-and-folders-in-outline.md)),
    a name starts 52 px into its row, and each level of depth adds the 14 px
    indent.
  - A framed node, a collapsing header, keeps ImGui's own places.
- **Colours:** derived from the current style each time a row is drawn, through
  `CurrentListRowColours()`:
  - selected: the accent (`ImGuiCol_CheckMark`) at alpha 0.20;
  - hovered: the window background mixed 5 % toward the text colour, `#1e1f22`
    on the chrome, a step from the mockup's `#1d1f23` in red and blue;
  - pressed: the accent at alpha 0.30.
  - The name of a selected row in the Explorer's trees is drawn in the emphasis
    colour, `ListRowNameColour(selected)` (`Core::EmphasisText`), unless the
    name has a colour of its own: a git status, a dimmed ignored file or a
    missing one.
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

A list that fills its panel puts the highlight where the mockup puts it, 6 px
from the panel's edges, and the margin scope gives everything else in the panel
the padding it had, so only the lists move. Pushing the padding around `Begin`
alone leaves the dock tabs, popups and children as they were. The tree's own
offsets replace ImGui's, which grow with the frame padding and the font, with
the mockup's lengths. A list that forgets its list body is caught where it is
drawn, in every test and debug build that draws it, rather than by a test of
each panel.

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
- The rows of the flush panels are highlighted 6 px from the panel's edges, and
  a tree's chevron, icon and name stand where the mockup puts them.
- A list that forgets its list body fails in the tests and debug builds that
  draw it, instead of drawing its text 2 px high.

### Negative

- A row is a pixel taller than a text line with the interface's item spacing,
  22 px against 21 px, so slightly fewer rows fit on screen.
- Every list has to lay its rows out inside a list body, and every popup begun
  inside one has to be begun inside a popup scope. A list that forgets draws its
  text up to 2 px above the middle of its rows, and a popup that forgets is
  spaced 3 px, not 5 px.
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
- A flush panel has to draw whatever is not a list inside a margin scope, or it
  touches the panel's edges. The scope narrows ImGui's work rect and content
  region for its life, internal state a change in ImGui does not reach.
- While a list shows its scrollbar, its rows' highlights end 18 px short of the
  panel's right edge, not 6 px.
- A list row laid out outside a list body stops a debug build at an assertion.
- The framed group headers of Claude Sessions run from one edge of the panel to
  the other.

### Neutral

- The notification history keeps its cards.
- The cost view's open turn takes the accent at a fifth, as any selected row
  does, in place of its own stronger wash.
- The first and last cells of a list table stand 8 px from the table's ends,
  where they stood at the cell padding or flush before.
- A search hit row, drawn in the code face, is a 17 px line plus the row
  spacing: 23 px.
- Outside a list body the text top is held above the middle, by 2 px at 100 %,
  so that a text line laid there still ends inside the row's line.
- Inside a list body, items that are not rows, such as a section's heading or
  an empty list's message, are spaced 3 px, not 5 px.
- The rows of Changes and the hits of Find in Files and References moved 8 px
  left with their lists; Changes keeps its own offsets inside a row. Problems'
  headings and problems stand where they stood, by the list's own indent.

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
  too. The list body is narrower: rows keep their own pitch, and the capped
  spacing only keeps a row's line from running past it.
- **An item spacing of 3 px for the whole interface.** One constant, but every
  menu, form and dialog would tighten by 2 px a line.
- **Rows of 22 px with the text held up to fit the interface's spacing.** The
  text would stand 2 px above the middle of every row.
- **ImGui's own row background, through `TableSetBgColor`.** ImGui fills it
  square from one end of the table to the other, with no inset or rounding.
- **A highlight per cell.** The rounding would cut across every column.
- **A square highlight across the row.** It would fill the row and span every
  column, but a table's rows would not read as a list's.
- **Draw the scrollbar over the rows.** ImGui keeps a window's content clear of
  its scrollbar's lane and draws the bar first, so the lists would need a
  scrollbar of the app's own.
- **Keep the panels padded and widen each list into the padding.** The list's
  width would carry the panel's content past its work rect and give the panel
  8 px to scroll sideways.
- **Every tool panel flush.** The console's lines, Settings and the Git view's
  table would each need margins of their own.
- **A test of each panel's list body, drawn through the panel.** Changes needs
  the document windows and their server, analysis and git ports, and Claude
  Sessions reads transcripts on a thread from a folder the environment names.
- **A warning in the log for a row outside a list body.** Nothing would fail.

## Related

- [0604](0604-draw-every-list-row-through-listrowselectable.md): amended by this
  record.
- [0603](0603-mark-whatever-opens-and-closes-in-a-list-with-the-one-dim-chevron.md):
  the tree row built on the list row; amended by this record, which places its
  chevron and label.
- [0613](0613-draw-the-chrome-darker-than-the-documents-and-diffs-it-frames.md):
  the chrome the hover lifts from.
- [0618](0618-head-a-panel-with-one-row-of-quiet-choices-and-icon-actions.md):
  the panel header a flush panel draws inside its margin.
- [0610](0610-toggle-tool-panels-from-a-strip-that-ignores-where-they-are-docked.md):
  the tool panels.
