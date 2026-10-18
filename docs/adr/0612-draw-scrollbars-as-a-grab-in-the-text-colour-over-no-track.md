# 0612. Draw scrollbars as a grab in the text colour over no track

Status: proposed
Date: 2026-10-18

## Context

ImGui's default scrollbar is a grey track with a lighter grey grab. The greys
are not in the palette, and on the dark chrome the track is a visible band down
the side of every list and panel. The default width is 14 px.

## Decision

- Draw the scrollbar track transparent.
- Draw the grab in the text colour at 12 % alpha, 20 % when hovered and 28 % when
  held. A light scheme follows on its own, since its text colour is dark.
- Make the scrollbar 12 px wide.
- Leave the editor's change markers on its scrollbar (0527) unchanged.

## Rationale

The text colour is the one colour every scheme defines against its background,
so a grab derived from it stays legible in both. With no track the scrollbar is
a hint beside the content rather than a band.

## Consequences

### Positive

- Scrollbars use only palette colours.
- Light and dark schemes need no separate values.
- Panels lose a band of grey at their edge.

### Negative

- At 12 % the idle grab is faint, and a reader may not notice a scrollbar until
  hovering it.
- With no track, the extent of the scrollable range is shown only by the grab.

### Neutral

- Scrollbar width goes through `Core::Scaled` like every other size (0534).

## Alternatives Considered

- **ImGui's defaults.** This lost because the greys come from outside the palette
  and sit on a near-black track.
- **The grab in the accent.** This lost because it competes with selection.

## Related

- 0527, 0534
