# 0525. Reserve a change column in the gutter whether or not git is present

Status: proposed
Date: 2026-09-26

## Context

The editor's gutter already keeps columns that draw nothing: a breakpoint
margin to the left of the line numbers and a fold arrow to their right, both
held open so that the debugger and code folding, when they arrive, do not
shift every horizontal position the reader has already looked at. The text
origin is measured from the gutter's total width, so any column that comes
and goes moves the code with it.

Change markers (0523, 0524) now need somewhere to draw. What they describe
exists only for a file inside a repository that git tracks: a folder with no
repository, an untracked file, an ignored one, or a document whose first
answer has not landed yet all have nothing to show. If the column appeared
only when there was something in it, opening a folder and then running
`git init`, or waiting the fraction of a second for a document's first
markers, would push the text sideways under the caret.

## Decision

- Reserve a 5 px change column between the breakpoint margin and the line
  numbers, in every editor, whether or not git is present and whether or not
  the document has markers. `GutterMetrics::ChangeWidth` holds it and
  `TotalWidth` includes it, so the text origin never depends on git.
- Draw a 3 px bar at the column's left edge for each added or modified line.
  The bar spans every row of a wrapped line, since it marks the line, not the
  row.
- Draw a deletion as a small filled triangle on the boundary above its
  anchor line, pointing into the text. An anchor past the last line sits on
  the bottom edge of the last line instead.
- Take the colours from the scheme's git gutter colours (`GutterAdded`,
  `GutterModified`, `GutterRemoved`), the same ones the Changes panel reads,
  through `EditorPalette`.
- Draw a staged change at 0.45 of its colour's alpha, and an unstaged one at
  full strength, so what still needs attention stands out from what is
  already settled.
- Bounds-check every line against the markers before drawing: the markers
  may describe a buffer a few edits old (0524), and a line they do not cover
  simply draws no bar.
- Drop the column, and with it the markers and their tooltip, only when the
  reader turns off "Change markers in the gutter" in the editor settings.
  The text then moves once, at the reader's own request. The reservation
  exists to stop it moving on its own when a repository comes or goes, which
  a deliberate switch is not. With the scrollbar's markers (0527) off as
  well, no document is diffed at all.

## Rationale

A reserved column costs a few pixels and buys a text origin that nothing
about the repository can move, which is the same trade the breakpoint and
fold columns already made. Fixing it in pixels rather than cells keeps it
narrow at every font size: it holds a bar, not a character, and scaling it
with the font would turn a hairline mark into a slab at large sizes.

## Consequences

### Positive

- The code never shifts when a repository appears, when a file becomes
  tracked, or when a document's first markers arrive.
- The gutter has one layout for every document, so the line numbers sit in
  the same place in a file under `bin/` as in one under `src/`.

### Negative

- A folder with no repository shows 5 px of empty gutter that will never
  hold anything, and so does every ignored or untracked file.

### Neutral

- The markers are drawn in the gutter pass, after the text, so they are
  pinned against horizontal scroll like the line numbers.
- Lens rows draw only the gutter fill, so a lens never carries a marker.
- A deletion's triangle reaches a few pixels into the row above its anchor,
  which the row loop has already drawn, so it lands on top of that row's
  fill rather than under it.

## Alternatives Considered

### Show the column only when the document has markers

Keeps the gutter as narrow as it was for anything outside git. Rejected:
the text would jump sideways the moment a document's first markers landed,
and again whenever a file moved in or out of being tracked, which is exactly
the shift the reserved breakpoint and fold columns exist to prevent.

### Colour the line number instead of adding a column

Needs no extra width at all. Rejected: it has no place to show a deletion,
which sits between two lines rather than on one, and it cannot tell a staged
change from an unstaged one without competing with the caret line's own
highlight of its number.

## Related

- [0523](0523-draw-change-markers-from-the-live-buffer-against-the-index-and-head.md)
  — what the markers describe and how they are built.
- [0524](0524-hold-change-markers-in-a-model-of-their-own-apart-from-gitmodel.md)
  — the model the gutter reads them from, and why they can lag the buffer.
- `Graphite/src/Editor/TextEditorWidget.cpp` — `GutterMetrics`,
  `DrawChangeMarker` and the column constants.
