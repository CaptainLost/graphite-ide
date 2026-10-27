# 0486. Insert non-document rows above RowMap rather than inside it

Status: proposed
Date: 2026-08-16

## Context

[0485](0485-take-code-intelligence-from-external-language-servers.md) has a
language server answer "find references", and the natural place to show the
count is a line drawn above the symbol it describes — `3 references` sitting
in the gutter's column, occupying a screen row of its own, the way an inlay
hint or a code lens does in other editors. That row corresponds to no
document line: it is not line *N* of the buffer wrapped an extra time, it is
a row `TextEditorWidget` draws that `TextBuffer` has never heard of.

`Core::RowMap` is the sole authority for the relationship between a screen row
and a document position. `RowAt` returns a `RowRef{Line, SubRow}` for every
row it knows about, and [0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md)'s
`CaretToVisual`/`VisualToCaret` in `WrapLayout.cpp` are built on the
assumption that this relationship is total and bijective on the rows that
exist: every row is part of some line, at some sub-row, and every reachable
caret position maps to exactly one row. Vertical caret motion, a click at a
pixel, a drag selection and `PageUp`/`PageDown` all go through one of those
two functions. A screen row that is not part of any line breaks the premise
those functions are written against.

## Decision

`Core::RowMap` is not told about a reference-count row, or about any other row
a lens draws that does not correspond to a document line. Caret arithmetic is
untouched: vertical motion, selection and the find bar keep calling
`CaretToVisual`/`VisualToCaret` exactly as they do today, entirely in document
rows, with no lens in sight. A Graphite-side `LensRows` sits only where a
*screen* row — one that may or may not be a document row — has to be
identified as one or the other, or turned into a height or a pixel offset.
Seven sites in `TextEditorWidget.cpp` do this — six that translate and one
that deliberately does not. Each line number below is the `LensRows` call
itself, not the function containing it:

- `DocumentHeightOf` (`:318`) asks `LensRows::TotalScreenRows`, not
  `RowMap::TotalRows`, for the height the scrollbar and the clipper size
  against — otherwise the scrollbar stops short of the file's true end by
  however many lens rows it never counted.
- The `ImGuiListClipper` loop (`:1752`) counts screen rows from
  `clipper.Begin`, and `LensRows::At` says, for each one, whether it is a
  lens row to draw a reference count on or a document row to hand to
  `Core::Text` for its tokens and caret paint.
- The mouse hit test (`:419`) divides a pixel offset by the row height to get
  a screen row, and `LensRows::At` converts that screen row before it ever
  reaches `VisualToCaret` — a click on a lens row opens references instead of
  placing the caret in text that is not there.
- Scrolling to the caret (`:981`) computes a document row with
  `CaretToVisual`, unchanged, and only then asks `LensRows::ScreenRowOf` to
  wrap that document row into a screen row before multiplying by the line
  height — the caret's own position is never computed in screen-row terms.
- `ComputeHoverIdentifier` (`:1538`) does the same division the mouse hit test
  does, for the pointer rather than for a click, and asks `LensRows::At` the
  same question: a lens row holds no text for an identifier to sit in, so
  Ctrl+hover finds nothing there rather than underlining whatever line the
  untranslated row would have named.
- The caret's screen position (`:1728`), which `DrawEditor` reports back so a
  request made from the keyboard has somewhere to open a popup near, wraps
  `caret.Visual.Row` through `LensRows::ScreenRowOf` before multiplying by the
  line height, exactly as scroll-to-caret does.
- `pageRows`, the row count `PageUp`/`PageDown` step by (`:325`), is left
  alone deliberately: it is a count of rows to move, not a row to look up, so
  it has nothing to translate and nothing to ask `LensRows` about.

The last two arrived after this record was first written, with go-to-definition
and Ctrl+hover. Their existence does not change the decision — both reach pixels
the same way the four before them do, through `LensRows` — but leaving them out
of the list above would have made the enumeration read as exhaustive when it was
not, and the enumeration is the only thing that makes "no untranslated site
exists" checkable.

## Rationale

Teaching `RowMap` about non-document rows would make `CaretToVisual` and
`VisualToCaret` non-bijective: rows would exist on screen that no caret
position maps to, and the caret would have to skip them on every vertical
move, every click and every drag. That skip is one more conditional in
functions [0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md)
already treats as the single source of truth for caret placement, and getting
the skip wrong is quiet — a caret that lands one row off from where a click
happened is a bug a screenshot does not obviously show, and moving up twice
from a line under a lens could either skip the lens row correctly or land the
caret inside it, silently, depending on whether every one of those call sites
remembered the exception. The design this record chooses avoids that failure
mode by not putting the exception in those call sites at all: vertical
motion never sees a lens row, because `CaretToVisual`/`VisualToCaret` keep
working purely in document rows, exactly as before this record. There is
nothing there to get quietly wrong.

`LensRows` living in Graphite and translating at the boundary fails loudly
instead: a call site that forgets to translate does not produce a subtly
wrong caret position, it draws a lens row's reference count where a line of
code should be, or vice versa — a rendering bug that is visible on the screen
it broke, not a caret arithmetic bug that only shows up under specific
navigation. It also keeps a lens as something `Core::Text` never has to know
exists, consistent with [0002](0002-keep-core-free-of-ide-knowledge.md):
reference counts and code lenses are IDE-specific decoration, not text-buffer
geometry.

## Consequences

### Positive

- `Core::RowMap`, `CaretToVisual` and `VisualToCaret` keep the bijective
  property they were written under: every row they know about is a real
  document position, so [0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md)'s
  reasoning about them does not need revisiting.
- A lens drawing wrong is visible where it happens — the row is off — rather
  than surfacing as a caret placed one row away from a click, which is a
  worse bug to trace because the symptom is far from the cause.
- `Core/tests` and `CoreTests` are unaffected: `RowMap` grows no new case for
  a screen concept it never needed to represent.

### Negative

- The four sites that map a screen position to or from a document one —
  `DocumentHeightOf`, the clipper loop, the mouse hit test, and scroll-to-caret
  — must each remember to go through `LensRows`, and a later call site that
  works in screen rows without doing so is a bug this decision does not
  prevent, only makes cheaper to find than the alternative would have.
- `pageRows`, the row count `PageUp`/`PageDown` step by, is the fifth site and
  is deliberately left untranslated: it is a count of rows to move, not a row
  to look up, so it overshoots by however many lens rows sit in the range it
  crosses. A page down that would land ten document rows lower lands fewer if
  lens rows sat in between, because the count consumed some of its budget on
  rows that are not document rows at all.

### Neutral

- Code folding is a different problem from a lens row: it hides real document
  lines that still exist, rather than inserting rows that never corresponded
  to one, and the mapping it needs — document line to screen row, skipping a
  hidden range — is a mapping `RowMap` is positioned to own directly, not one
  a Graphite-side translator can supply from outside. Folding, when it
  arrives, supersedes this record for the folded case rather than extending
  it; a lens row and a folded line are not the same kind of gap and are not
  expected to share a fix.

## Alternatives Considered

### Teach RowMap a non-document row kind

`RowRef` gains a tag distinguishing a document row from a decoration row, and
`RowAt` returns the tag alongside the position. Keeps the row-to-position
mapping in one place instead of two. Rejected because it is the option the
Rationale argues against directly: `CaretToVisual` and `VisualToCaret` would
have to check the tag on every call and skip or special-case decoration rows,
turning code that is currently a straightforward lookup into code that
branches on a case most calls do not care about, for a concept — a
reference-count lens — that `Core::Text` has no other reason to know exists.

### Give TextEditorWidget its own full row-to-document mapping, independent of RowMap

Skip translation entirely and have the widget compute document positions
itself, wrapping included, without going through `Core::RowMap` at all.
Rejected: it duplicates the wrap-layout arithmetic
[0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md) already
built and tested in `Core::Text`, and two implementations of "which row is
this document position on" drifting apart is a worse failure mode than the
one this record accepts — at least a `LensRows` translation sitting in front
of a single shared `RowMap` has one wrap implementation to get right.

## Related

- [0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md) — the
  `CaretToVisual`/`VisualToCaret` contract this record keeps bijective.
- [0002](0002-keep-core-free-of-ide-knowledge.md) — why a reference-count lens
  is Graphite's concept to hold, not Core's.
- [0485](0485-take-code-intelligence-from-external-language-servers.md) — the
  language-server answer a lens row displays.
- [0646](0646-place-non-document-rows-as-blocks-from-a-source-the-language-server-does-not-own.md)
  — amends this record: the sites below now translate through `ScreenRows`,
  which adds a block of rows from a source other than the language server.
- `Core/src/Core/Text/RowMap.h`, `Core/src/Core/Text/WrapLayout.cpp` — the
  types and functions this record keeps document-only.
- `Graphite/src/Editor/TextEditorWidget.cpp` — the nine sites:
  `DocumentHeightOf`, the clipper loop, the mouse hit test, scroll-to-caret,
  `ComputeHoverIdentifier`, the caret's screen position and both halves of the
  scroll anchor — the correction that reads it and the record that takes it —
  translate through `LensRows`; `pageRows` deliberately does not.

  The scroll anchor is the clearest illustration of why this record's rule is
  worth its cost. It has to hold a place in the document across a change to the
  screen-row mapping, which is only expressible because the two are separate
  things: it stores a line, converts to a document row through `RowMap`, and
  converts that to a screen row through `LensRows`. Had lens rows been inserted
  into `RowMap` itself, there would be no document coordinate left to anchor to.
