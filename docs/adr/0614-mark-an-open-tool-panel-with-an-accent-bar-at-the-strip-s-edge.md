# 0614. Mark an open tool panel with an accent bar at the strip's edge

Status: proposed
Date: 2026-10-18

## Context

0610 marks an open tool panel on the strip with the pressed-button fill
(`SurfaceActive`) behind its icon. That fill is as loud as a button held down,
and a strip with several panels open reads as several buttons stuck in. The
strip also draws a border down its right edge, although its background is the
colour of the side panel it sits against, so the border cuts a seam through what
reads as one surface.

## Decision

This amends 0610's last Decision bullet, the exception to 0602 for the strip's
buttons, which lists "an open fill in `SurfaceActive`" among what a strip button
needs. The bar below takes the place of that fill, and the exception to 0602 now
rests on the bar rather than the fill.

- An open item has no fill. A 2 px accent bar runs at the strip's left edge,
  5 px short of the button at each end, with rounded ends.
- The glyph is drawn in the text colour when the item is open or hovered, and in
  the disabled-text colour when it is closed.
- Hover adds the `SurfaceHovered` fill behind the icon.
- The strip draws no border at its right edge. Separators between groups keep
  the border colour.
- Everything else in 0610 stands, including two panels of one node both reading
  open.

## Rationale

A thin bar in the accent says "this one is on" without competing with the icon,
and it leaves the fill free to mean hover alone. Dropping the border lets the
strip and a side panel share one surface.

## Consequences

### Positive

- An open item is told apart from a hovered one.
- The strip merges with the panel beside it.

### Negative

- The bar is small, so on a strip with many icons the open one is found by the
  bar rather than at a glance.
- With no right border, the strip's edge against a panel of a different colour
  is no longer drawn.

### Neutral

- Geometry is unchanged. The bar's width and inset go through the strip's
  whole-pixel helper (0534).

## Alternatives Considered

- **0610's `SurfaceActive` fill.** This lost because it is as loud as a pressed
  button.
- **A bar at the strip's right edge.** This lost because it would touch the panel
  and read as the panel's border.

## Related

- 0602, 0610
