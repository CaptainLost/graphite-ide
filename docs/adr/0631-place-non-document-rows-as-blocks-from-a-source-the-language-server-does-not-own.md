# 0631. Place non-document rows as blocks from a source the language server does not own

Status: proposed
Date: 2026-10-26

## Context

[0486](0486-insert-non-document-rows-above-rowmap-rather-than-inside-it.md)
keeps `Core::RowMap` document-only and has a Graphite-side `LensRows`
translate at every site where a screen row may or may not be a document row.
Lens rows were the only rows of that kind, and they have two properties the
sites came to rely on: each anchor adds exactly one row, and the rows exist
only through `LensModel`, which exists only for a language with a language
server. A file without a server has no lens model, so its screen rows are its
document rows.

The editor is to open a line history under a line: a peek about nine rows
tall that pushes the lines below it down and scrolls with the text. It is a
non-document row in exactly 0486's sense, but it is a block of several rows,
and it belongs to any file under git, whether or not its language has a
server. Neither property of a lens row holds for it.

## Decision

Keep 0486's decision: `Core::RowMap`, `CaretToVisual` and `VisualToCaret`
stay in document rows, and vertical motion, selection and the find bar never
see a row that is not a document row. Generalise only the translator:

- `ScreenRows` (`Graphite/src/Editor/Overlays/ScreenRows.h`) is the one map
  between screen rows and document rows. `DrawEditor` builds it once per
  frame from the lens rows and an optional `PeekPlacement`: a block of
  `Rows` rows placed before a document row, where "under line L" means
  before the first row of line L+1, or before the row count for the last
  line.
- The placement comes from the editor itself, which holds the `RowMap` the
  placement is computed against. It does not come from `LensModel` or from
  anything else that exists only with a language server; without a lens
  model the lens rows are empty and the block still has its place.
- On one document row R the order is: the block's rows, directly under the
  line above; then R's lens row, which belongs to the declaration on R; then
  R itself.
- `ScreenRows::At` answers a kind, `Document`, `Lens` or `Peek`. A site that
  asked whether a row was a lens now asks whether it is a document row, so a
  block row is treated as a lens row is: no caret placement, no context
  menu, no change hover and no hover target. Only the lens drawing and the
  lens label's hover and click ask for a lens row by name.
- Every screen-row site goes through `ScreenRows`: `DocumentHeightOf`;
  `RowUnderMouse`, behind the lens hover and the mouse cursor;
  `ApplyMouseToCaret`; `ScrollToCaret`; `ComputeMouseHit`, behind the
  Ctrl+hover identifier, the diagnostic hover and the symbol hover;
  `AnchorOfToken`; `ComputeChangeHover`; `OverviewLineTop`; the caret's
  screen position and its overview mark; the clipper loop; and both halves
  of the scroll anchor. No site calls `LensRows` for a translation.
- `LensRows` is unchanged and remains the lens model's storage. `ScreenRows`
  holds it by address and reads it on every call, so an edit that shifts the
  anchors during a frame is seen by the rest of that frame.
- `RowsPerPage`, the count `PageUp` and `PageDown` step by, still does not
  translate.

## Rationale

A block row stands in the same relation to the document as a lens row does:
it corresponds to no caret position. 0486's argument for keeping such rows
out of `RowMap` therefore applies unchanged, and only the translator in front
of `RowMap` needs to learn a second source. Building one map from both
sources gives every site a single answer for a screen row, so the sites
cannot disagree about where the block sits, and none of them has to know how
many kinds of non-document row exist. Keeping the block out of `LensRows`
keeps it out of a model that a file without a language server never has.

## Consequences

### Positive

- A file whose language has no server can still show a block between its
  lines.
- The sites kept their shape. A row that is not a document row is non-text
  at every site by default, so a later kind of block row starts out with no
  caret, menu or hover on it rather than having to be excluded site by site.
- `LensRows`, `LensModel` and their tests are untouched.

### Negative

- At most one block. Several blocks open at once would need the placement to
  become a sorted list and `At` to search it.
- A screen-row lookup does two binary searches, one for the block's top and
  one in the lens rows, where it did one.
- `PageUp` and `PageDown` overshoot by the block's rows as well as the lens
  rows in the range they cross, which with a block of nine rows is most of a
  page.
- A drag across the block extends the selection to the row below it, as a
  drag across a lens row does, because a block row answers the document row
  it sits above.
- As with 0486, a later site that works in screen rows without going through
  `ScreenRows` is a bug this record makes cheaper to find, not impossible.

### Neutral

- This amends 0486. Its decision and rationale stand; its list of sites is
  replaced by the one above, and `LensRows` is now read through `ScreenRows`
  rather than at each site.
- A clipper step can begin inside the block. Drawing it once from its own
  first row, the mouse inside it, and Escape belong to the code that opens
  it, not to this map.

## Alternatives Considered

### Give each lens anchor a height

`LensRows` would hold a row count per anchor, and the block would be an
anchor nine rows tall. One structure instead of two. Rejected: `LensRows`
lives inside `LensModel`, which a file without a language server does not
have, so such a file would have nowhere to put the block; and a block and a
lens on the same document row would have to share one anchor, which mixes a
row the lens model rebuilds on every answer with one the reader opened.

### Make the block extra rows of the line above in RowMap

The block would be wrapped rows of line L that hold no text. Rejected by
0486's own rationale: `CaretToVisual` and `VisualToCaret` would gain rows no
caret position maps to, and every vertical move, click and drag would have
to skip them.

### Float the block over the text

The block would be a window drawn over the lines below the one it describes,
taking no rows at all. No translation needed. Rejected: it would hide the
code under it instead of pushing it down, the scrollbar and the clipper would
not know its height, and the reader could not scroll to the lines it covers.

## Related

- [0486](0486-insert-non-document-rows-above-rowmap-rather-than-inside-it.md)
  — the record this amends.
- [0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md) — the
  `CaretToVisual`/`VisualToCaret` contract both records keep document-only.
- `Graphite/src/Editor/Overlays/ScreenRows.h`,
  `Graphite/src/Editor/TextEditorWidget.cpp` — the map and its sites.
