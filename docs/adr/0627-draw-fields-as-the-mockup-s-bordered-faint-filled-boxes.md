# 0627. Draw fields as the mockup's bordered, faint-filled boxes

Status: proposed
Date: 2026-10-19

## Context

The mockup draws a text field 26 px tall, with 6 px corners, a 1 px border in the
line colour, an inner padding of 8 and a fill of `rgba(255,255,255,.035)`. On the
chrome that fill shows as `#1c1d20`. A field's search glyph is 12 px and dim, with
the text 6 px after it, and the placeholder is dim. The app drew every field as a
borderless 22 px slab in the surface colour (`#25282d`), with 3 px corners: the
frame padding was `(8, 4)`, `FrameBorderSize` 0 and `FrameBg` the surface.

The mockup's buttons that show a word have no border. They are clear at rest,
`#32363d` under the pointer and rounded 5 px. The app filled them with the hover
surface at rest.

`FrameBorderSize` is one global value. ImGui reads it for every frame it draws
with a border: text fields, combos, sliders, the steps of a number field, radio
rings, colour swatches, progress tracks and buttons. Five places read `FrameBg`
as "the surface": settings cards, the settings sidebar, a diff's hunk headers,
the spinner's track and the sessions' segment buttons.

## Decision

- **Metrics.** Set `FramePadding` to `(8, 5)`, so a field is 26 px at the 16 px
  interface size. Set `FrameRounding` to 6 and `FrameBorderSize` to 1. The
  border is drawn in `ImGuiCol_Border`, the line colour.
- **Fill.** Make `FrameBg` see-through: white at 3.5 % on a dark scheme, and
  the text colour at 3.5 % on a light one. On the chrome it shows as the chrome
  lerped 3.5 % toward white (`#1c1d20`). On a card or a popup it lifts that
  surface by the same step. `FrameBgHovered` and `FrameBgActive` stay the hover
  surface. A text field fills with `FrameBg` alone, so it has no hover fill,
  as in the mockup.
- **Per kind.** Every frame ImGui draws with a border keeps the hairline: text
  fields (single- and multi-line), combos, sliders, number fields with their
  steps, radio rings, colour swatches and progress tracks. These are value boxes
  and read as fields. Text buttons are the one kind without it.
- **Text buttons.** Draw every button that shows a word through
  `Core::TextButton` or `Core::SmallTextButton`. They push `FrameBorderSize` 0
  and a 5 px rounding. `ImGuiCol_Button` is the window colour at alpha 0, so the
  button is clear at rest, and `ButtonHovered` and `ButtonActive` stay the hover
  surface. `scripts/Test-NoImGuiButton.ps1` fails on any `ImGui::Button` or
  `ImGui::SmallButton` outside `TextButton.cpp`. An engaged icon button rests on
  `ButtonHovered`, no longer on `Button`.
- **Surface.** Read the palette's surface through `Core::SurfaceColour()`, which
  `Theme::Apply` keeps in `PopupBg`, never through `FrameBg`. Cards, the
  settings sidebar, hunk headers, the spinner's track and the segment buttons
  use it.
- **Fields on surfaces.** Take out the pushes that raised a field one step above
  the surface it sits on. These are the settings cards' frame colours, the
  settings search's own fill and rounding, and the modal questions' window-colour
  fields. The see-through fill and the border now set a field apart from any
  surface.
- **Filter field.** Draw the glyph at 12 px, 8 px in from the field's start and
  centred in its height. Start the text 6 px after the glyph.
- **Heights that are not fields.** Pin them. The status bar keeps its 23 px, and
  pushes its own frame padding (0626). A list row's pitch is a text line plus the
  row spacing, whatever the frame padding is. Bars made of controls follow the
  control height: the find bar, info bars, the notifications header and a
  settings row. Dock tab bars follow the global padding, 26 px, until their own
  record sets them.

## Rationale

A see-through fill is the mockup's own rule, and it is the only rule that keeps a
field one step above whatever is behind it without a push per surface. A global
border reaches every field-like frame ImGui draws, including fields in code that
calls ImGui directly. There are fewer text buttons than field-like frames, and
one helper and a lint cover them.

## Consequences

### Positive

- Every field in the app has the same box: filter fields, the find bar, the
  settings search, commit messages, combos and sliders.
- Text buttons recede until hovered, as in the mockup.
- Cards and popups no longer depend on a slot named for fields.

### Negative

- Every field-like frame grows 2 px, and every row of controls built from them
  grows with it.
- A combo's arrow box is clear at rest, so the faint fill stops short of the
  arrow. At 3.5 % this is barely visible.
- A number field's step buttons keep a border, because ImGui draws them inside
  the field.
- A new raw `ImGui::Button` gets a border until the lint flags it.

### Neutral

- The toggle's track reads `FrameBgActive`, which is still the hover surface.
- 0624 noted that a settings card's raised field looked darker under the
  pointer. That push is gone, and the card's fields now hover in the hover
  surface.

## Alternatives Considered

- **An opaque fill, the chrome lerped 3.5 % toward white.** This lost because a
  field on a card or a popup would then be darker than its surface, and each
  surface would need its own push.
- **`FrameBorderSize` 0, with the border drawn by each field.** This lost
  because fields are drawn from more places than buttons, many of them through
  ImGui directly, and the border would be missing wherever one was forgotten.

## Related

- 0588, 0599, 0601, 0613, 0624, 0626
