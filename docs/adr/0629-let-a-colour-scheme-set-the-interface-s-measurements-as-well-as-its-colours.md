# 0629. Let a colour scheme set the interface's measurements as well as its colours

Status: proposed
Date: 2026-10-20

## Context

A scheme held colours and nothing else. The interface's corner radii, border
width, scrollbar width and the gap between docked panels were constants in
`Core::Theme::SetMetrics`. A scheme that wants its panels apart, as cards, or
squarer corners could not say so, and a built-in could not differ from another
in anything but colour.

`SetMetrics` also runs on every change of interface scale: `ScaledStyle` builds a
fresh `ImGuiStyle` from it and copies only the colours across. A measurement set
once on the live style would therefore be lost the next time the scale changes.

## Decision

- **Five measurements.** A scheme may set `LayoutMetric::PanelGap`,
  `PanelRounding`, `ControlRounding`, `BorderWidth` and `ScrollbarWidth`, in
  unscaled pixels. A built-in sets all five; a user scheme sets any of them, as it
  does colours, and inherits the rest from its base.
- **Mapping.** `Core::StyleMetrics` carries them, inside `Core::ChromePalette`, so
  Core learns nothing about schemes. `SetMetrics` maps them to ImGui:
  - the gap is `DockingSeparatorSize`;
  - panel rounding is `WindowRounding`, `ChildRounding` and `PopupRounding`;
  - control rounding is `FrameRounding`, with `GrabRounding` at one half and the
    selectable and menu-item rounding at two thirds of it;
  - border width is the window, child, popup and frame border sizes;
  - scrollbar width is `ScrollbarSize`, and its rounding is half of that.
- **Defaults.** 1, 4, 6, 1 and 12: the values the interface had, so the original
  dark and light schemes look as they did.
- **Reaching the live style.** `Theme::Apply` records the palette's metrics, and
  `ScaledStyle` takes them from there. When they change, `Theme::TakeMetricsChanged`
  reports it once and `ImGuiRenderer::UpdateStyleScale` rebuilds the style at the
  scale it already has.
- **File.** A scheme file stores a measurement as `layout.<name>`, such as
  `layout.panelGap`. A value outside its range is clamped to it; one that is not a
  number is ignored with a warning.
- **Settings.** Appearance gets a Layout card of five sliders. A built-in shows its
  values disabled; a copy edits them, and a reset puts back an inherited one.
- **Built-ins.** Eleven schemes join `dark` and `light`: `graphite`,
  `graphite-bright`, `graphite-warm`, `graphite-cards`, `graphite-light` and
  `gruvbox-{dark,light}-{hard,medium,soft}`. Each is a short palette of surfaces,
  text and syntax colours turned into every slot over the dark or light scheme, in
  `DerivedSchemes.cpp`. Only `graphite-cards` sets measurements: a 6 px gap and 8 px
  panel corners.

## Rationale

The measurements belong with the colours because they are what makes a look: a
scheme that is cards, or flat, is both. Carrying them in the palette keeps Core free
of schemes, and rebuilding the style from the recorded metrics is the only way a
scale change does not undo them.

## Consequences

### Positive

- A scheme can differ from another in shape, not only colour.
- Adding a built-in is one palette, not a table of every slot.
- Existing scheme files load unchanged: they set no `layout.` keys.

### Negative

- `Theme` keeps the applied metrics in a file-local variable, a global that the
  process shares. Tests that apply other metrics must put the defaults back.
- A gap wider than 1 px shows the chrome colour between panels, so it reads as a
  gap only where the panels are filled with another colour or bordered.
- Other measurements (the height of a row, the shape of a tab, shadows, the padding
  of a panel) are not scheme settings; they are drawn by code that assumes its own
  values.

### Neutral

- The scheme list in Settings grows from two entries to thirteen.

## Alternatives Considered

- **A separate "shape" setting beside the scheme.** One more choice for the user to
  keep in step with the colours, and a built-in could not carry its own.
- **Setting the measurements on the live style only.** Simpler, but a change of
  interface scale rebuilds the style and loses them.
- **Writing each new built-in out as a full table, as the dark and light are.** Some
  120 lines a scheme for what is a dozen choices, and every colour a chance to
  disagree with the rest.

## Related

- [0627](0627-draw-fields-as-the-mockup-s-bordered-faint-filled-boxes.md) sets the
  frame metrics this record makes configurable.
