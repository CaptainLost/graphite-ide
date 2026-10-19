# 0624. Show a press in the hover surface and read the accent only from its own slot

Status: proposed
Date: 2026-10-19

## Context

ImGui gives every frame, header and button three colours: at rest, hovered and
active, which is held under the pointer. The theme hovered all three kinds in
`SurfaceActive` (`#3E434C`) and filled a held header or button with the accent,
so a click flashed saturated `#4285F4` on buttons, the window buttons, menus
and the tab close button. The mockup hovers every control in one surface,
`#32363D`, and has no pressed state. The focus ring (`NavCursor`) and a held
splitter (`ResizeGripActive`) were never set and kept ImGui's own `#4296FA`, a
different blue from the accent.

Two places read a held slot as the accent: the chosen segment of the sessions'
segmented choices took `ButtonActive`, and the Appearance page lit a colour found
from the settings search in `HeaderActive`.

## Decision

- Hover frames, headers and buttons in `SurfaceHovered`: `FrameBgHovered`,
  `HeaderHovered` and `ButtonHovered`.
- Show a press in the same colour: `FrameBgActive`, `HeaderActive` and
  `ButtonActive` are `SurfaceHovered`. There is no pressed colour, and the accent
  is never a fill that flashes under a click.
- Keep `Header` at `SurfaceHovered`. A menu fills its open item, and a combo its
  current item, in `Header`, so the open state keeps its fill.
- Draw the focus ring and a held splitter in the accent: `NavCursor` and
  `ResizeGripActive`.
- Read the accent from `ImGuiCol_CheckMark`, the slot the theme sets to the
  accent and to nothing else, never from a hovered or active slot. List rows and
  the branch list already did; a chosen segment and the Appearance page's light
  now do too.

## Rationale

One hover colour everywhere is what the mockup draws, and a press that keeps it
leaves the accent for marks that mean something: a selection, an open tool, a
tick. A slot named for a state is the wrong place to look for a colour, as the
two readers of the held slots showed when the held colour changed.

## Consequences

### Positive

- A click no longer flashes the accent anywhere.
- The focus ring and a dragged splitter are the palette's blue.
- A change to the hover or press colours cannot recolour a selection by accident.

### Negative

- A press has no feedback of its own beyond the hover fill.
- Where rest and hover share `Header`, nothing changes under the pointer: a
  framed collapsing header, such as a session group's row, and the current item
  of an open combo look the same hovered or not.
- `FrameBgActive` is now the hover surface, so the toggle's track at rest and a
  settings card's raised field under the pointer, which read it, are a step
  darker than they were.

### Neutral

- `SurfaceActive` is left to the resize grip of a floating window; it fills
  nothing a person clicks.

## Alternatives Considered

- **A pressed colour one step above the hover, `SurfaceActive`.** This lost
  because the mockup has no pressed state and the step reads as a flash of grey.
- **Keeping the accent for a held header or button.** This lost because it is
  the flash the mockup does not have.

## Related

- 0490, 0588, 0611, 0613, 0616
