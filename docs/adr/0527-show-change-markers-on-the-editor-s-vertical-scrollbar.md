# 0527. Show change markers on the editor's vertical scrollbar

Status: proposed
Date: 2026-09-27

## Context

The gutter shows how each line differs from git (0523, 0525), but only for
the lines on screen. In a long file, finding the changes means scrolling
through all of it. The editor's vertical scrollbar is ImGui's own. It
already spans the whole document, and `imgui_internal` gives its rectangle.
The markers the gutter draws are already held per document, with a hunk
list alongside them (0526).

The scrollbar has to stay a scrollbar: its grab must remain visible, and
clicking or dragging it must behave as before.

## Decision

- Draw each hunk of `ChangeMarkers::Hunks` on the vertical scrollbar, as a
  4 px strip centred on its track. Use the gutter's colours: added, modified and
  removed. Draw staged changes at the same 0.45 alpha the gutter uses.
- Place a hunk by where its lines sit in the laid-out document, in screen
  rows, with wrapped lines and lens rows included (0486). Measure against
  the content height ImGui's scrollbar spans, and inside the same inset its
  grab moves within, so a mark lines up with the grab when its line is at
  the top of the view. The inset comes from `style.ScrollbarPadding`, as it
  does in ImGui's own scrollbar.
- Make no mark shorter than 2 px, and draw a deletion as a mark of that
  height at its boundary.
- Draw staged marks before unstaged ones, so an unstaged change is on top
  where the two overlap. This is the precedence the gutter gives it.
- Draw only. The marks take no input and add no hit test, so the
  scrollbar's own behaviour is unchanged.
- Keep the arithmetic in a pure `BuildChangeOverview`
  (`Git/ChangeOverview.h`), given the layout as a line → fraction function,
  so it is tested without ImGui.

## Rationale

A narrow strip down the middle keeps the grab readable on both sides of it
without moving or resizing ImGui's scrollbar. Placing marks by screen row rather than by line number is what
keeps them honest in a wrapped document or one with many lenses. Working
from the hunk list rather than the per-line markers keeps the cost per frame
to the number of changes, not the length of the file.

## Consequences

### Positive

- The whole file's changes are visible at a glance, and the scrollbar
  already takes the reader to them.

### Negative

- A staged hunk that unstaged edits have split is drawn over its whole span,
  under the unstaged marks that interrupt it. The unstaged colour is on top
  there, so the picture reads right, but the staged mark is not exact.
- A file with no scrollbar shows nothing, even when it has changes. The
  gutter already shows all of them there.
- The marks cover the middle of the grab where the two meet, since they are
  drawn after it.
- Once a very long file shrinks the grab to its minimum size, the grab and
  the marks drift apart towards the bottom of the track, by up to that
  minimum size. The marks stay proportional to the document, while the grab
  does not.
- A mark ends at the top of the line after its hunk. When that line has a
  lens above it, the mark also covers the lens row: one screen row, which is
  less than a pixel on the track in all but short files.

### Neutral

- Only git changes are shown. Diagnostics or find matches on the scrollbar
  would need the track's width shared between kinds of mark, which is a
  decision for when they are wanted.

## Alternatives Considered

- **A separate overview column beside the scrollbar.** It would leave the
  grab untouched. It lost because it costs horizontal space in every editor
  and duplicates what the scrollbar already maps.
- **Marks across the full track width.** Simpler, but they hide the grab
  wherever a change sits under it.
- **Marks over the left half of the track.** Tried first. The grab then
  showed only on its right, and next to a bright mark it was barely visible.

## Related

- 0486 Insert non-document rows above RowMap rather than inside it
- 0523 Draw change markers from the live buffer against the index and HEAD
- 0525 Reserve a change column in the gutter whether or not git is present
- 0526 Highlight what a changed line lost by its shared prefix and suffix
