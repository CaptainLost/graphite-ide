# 0495. Draw a side-by-side diff in one scrolling region

Status: proposed
Date: 2026-08-21

## Context

A side-by-side diff shows the same file twice, and the two halves are only
useful next to each other: the reader compares row against row. The obvious
construction is one scrolling child window per half, and that is how the view
was first built.

It does not work. Each ImGui child window owns its scroll position, so
scrolling one half leaves the other where it was, and the rows the reader was
comparing no longer sit opposite one another. Nothing in ImGui couples them.
Neither does the fact that both halves walk the same row list under the same
clipper — that makes a row index land at the same offset within each half, not
at the same place on screen.

The repair that suggests itself is to copy one half's scroll position to the
other each frame. It cannot be made to work without a visible seam: the halves
are drawn in order, so whichever is drawn first can be told the other's
position from last frame, but the second can only be caught up on the next
frame. Under a wheel or a scrollbar drag that is a half that trails the other
by a frame, which is the artefact the coupling exists to remove.

## Decision

Draw both halves inside a single scrolling region. Give each half a clipping
rectangle pinned to the region's own, and position its text in the region's
content coordinates so it slides under that rectangle as the reader scrolls.

Split the region at half of what the reader can see, not at half of what the
region was asked to fit into: a scrolling region takes a border and, once its
rows stop fitting, a scrollbar off the width it was given.

## Rationale

There is nothing to synchronise. One region has one scroll position, so the
halves cannot disagree about where they are, and no arrangement of frames can
make them. A property that holds by construction does not need a mechanism to
maintain it, and cannot be broken by a later change that forgets the mechanism
exists.

## Consequences

### Positive

- The halves cannot drift apart, in either axis, under any input.
- One scrollbar rather than two showing the same position twice.
- The split, the row highlights and the separator are pinned to the region, so
  they stay put while only the text moves.

### Negative

- Horizontal scrolling moves both halves together. A rename where one side's
  lines are far longer than the other's cannot have one side scrolled
  independently. This is the real cost and it was accepted deliberately: both
  halves are the same file, and a reader comparing them wants the same column
  of each.
- The scrollbar's reach is set from the widest line drawn so far rather than
  the widest line in the file, because only the rows that have been on screen
  have been laid out — which is the whole point of the clipper. The width must
  therefore be kept across frames, and kept means knowing when it stops
  applying: see `DiffMeasurement` in `Graphite/src/Git/DiffRows.h`.
- The content width handed to the region is not simply a width. ImGui measures
  content from the padded corner and compares it against the unpadded inner
  width, so a content width equal to the visible width leaves a scrollbar with
  two paddings of travel and nothing to reveal. The correction lives in
  `SideBySideContentWidth`, where a test can reach it.

### Neutral

- The unified view keeps its own single region and is unaffected.
- Each half is clipped once per clipper step rather than once per row: pushing
  a clipping rectangle over anything already drawn starts a new draw command,
  so alternating between the halves row by row would cost two commands a row.

## Alternatives Considered

**Two child windows with their scroll positions copied between frames.** The
smaller change, and it keeps a horizontal scrollbar per half, which is strictly
more capable. Rejected because the copy cannot be made lag-free in one frame
for both directions, and a half that trails the other under the wheel is the
same complaint in a subtler form.

**Two child windows, one with its scrollbar hidden, the visible one driving
both.** Halves the visual noise without addressing the drift, and asymmetric:
input landing on the half without the scrollbar still has to travel a frame.

**One region with a native horizontal scrollbar spanning both halves laid out
end to end.** Simple, and wrong at the moment it matters: the halves would sit
at zero and one pane-width in a content space wider than the viewport, so
scrolling far enough to read a long line scrolls the other half off the screen
entirely, which is the one thing a side-by-side view may not do.

## Related

- [0479](0479-write-code-in-the-house-style.md) — the house style these files
  are written in.
- [0490](0490-take-colours-from-the-active-scheme.md) — where the separator's
  colour comes from.
