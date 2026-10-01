# 0534. Scale the interface by the monitor and by a user setting, and keep code text size separate

Status: proposed
Date: 2026-10-01

## Context

GLFW makes the process per-monitor DPI aware, so Windows does not stretch the
window on a monitor set to 150%. Until now nothing else did either: the font
was baked at 15 px, the theme's paddings were fixed, and every length the
application wrote in pixels stayed at its 100% size. On a laptop at 150% the
IDE came up small and sharp, and moving it to a monitor at another scale
changed nothing.

Two other forces shaped the decision. People expect to make the whole
interface larger without changing a system setting. And the editor already has
its own zoom on Ctrl+scroll, which changes the size of code but not of the
chrome around it.

## Decision

- Scale the interface by the product of two factors: the scale of the monitor
  a window is on, and an interface scale the user picks in Settings >
  Interface from a fixed list (75% to 200%), stored as `interfaceScale` in
  `graphite.json`.
- Text follows per window. Set `io.ConfigDpiScaleFonts`, so ImGui keeps
  `style.FontScaleDpi` at the scale of the current window's monitor, and carry
  the user's scale in `style.FontScaleMain`.
- Style sizes follow the main window's monitor, because ImGui has one style.
  `ImGuiRenderer::UpdateStyleScale` runs before every `NewFrame` and, when the
  scale has changed, replaces the style with `Core::ScaledStyle`: ImGui's
  default sizes with `Theme::SetMetrics` on top, multiplied by the scale, with
  colours and font scales carried over. `Theme::Apply` paints colours only.
- Write any length in pixels at its 100% size and pass it through
  `Core::Scaled` where it is used, inside the window it is drawn in. A length
  derived from the font or the style already follows and needs nothing.
- Keep the size of code text a separate setting from the interface scale.
  Ctrl+scroll over a view of code changes the code size; the interface scale
  changes everything, code included.

## Rationale

Building the style from defaults at each change of scale makes the operation
idempotent, so live changes - a window dragged across monitors, a setting
picked - cannot compound. Splitting text from sizes follows what ImGui can do:
per-viewport font scale exists, per-viewport style does not. A list of scales
rather than a slider keeps the interface from rebuilding under the pointer on
every frame of a drag.

## Consequences

### Positive

- The IDE comes up at the size the system asks for, and follows the main
  window from monitor to monitor without a restart.
- A user can enlarge or shrink the whole interface from Settings.

### Negative

- A panel undocked onto a monitor at another scale gets text at that
  monitor's scale but paddings at the main window's.
- Every new pixel constant is a chance to forget `Core::Scaled`, and nothing
  catches the omission but looking at the interface at a scale other than
  100%.
- Table column widths that ImGui saved at one scale are multiplied on loading
  at another, and come out up to a pixel narrower than the text they were
  sized to; text cut to fit a cell is given that pixel back.

### Neutral

- Dock node sizes, floating window sizes and the splitter heights panels keep
  in members are pixels and are not rescaled; the panels keep the absolute
  size the user gave them.
- ImPlot's own style - line weights, plot padding - is not scaled yet, so the
  Claude cost and request charts draw thin lines at large scales.
- Below a scale of 1, ImGui's `ScaleAllSizes` would truncate every 1px line to
  nothing; `ScaledStyle` keeps each line that was drawn at least a pixel
  thick.

## Alternatives Considered

- **Scale once at startup from the primary monitor.** Simpler, but a window
  moved to a monitor at another scale keeps the wrong size until restart,
  which is the everyday case on a laptop with an external monitor.
- **Scale the live style with `ScaleAllSizes` on each change.** It multiplies
  whatever is there, so two changes compound and rounding drifts; it needs a
  saved baseline anyway, which `ScaledStyle` is.
- **Let Ctrl+scroll change the interface scale.** One control instead of two,
  but there would be no way to enlarge code alone, which is what the editor's
  zoom is for.

## Related

- [0531](0531-keep-per-user-state-in-the-user-s-profile-not-beside-the-executable.md)
  - where `graphite.json` and the layout file live.
