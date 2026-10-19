# 0620. Show problems and the server's health as glyphs and colours in the status bar

Status: proposed
Date: 2026-10-19

## Context

The status bar was one run of text in one colour: "2 errors, 5 warnings", the
server's words ("C++: clangd ready"), the background pass, the caret position,
the encoding and the line ending, joined by runs of four spaces. An error could
not be told from a warning, or a ready server from a failed one, without reading
the words. The caret position was drawn in Inter, whose digits are proportional
([0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md)), and
the right-hand cluster is right-aligned, so everything to its left moved as the
caret did. The branch, the change count and the server's words take a click but
looked like the inert text beside them; only the bell had a fill under the
pointer, one text line tall. The read-only warning was appended to the path in
parentheses.

The bar draws from `StatusMessages`, data that `GraphiteLayer` builds each frame
([0535](0535-show-git-state-in-the-status-bar-read-only-and-keep-git-actions-in-the-panels.md)).
It knows nothing of the diagnostics model, the language services or the scheme.

## Decision

- **Problems.** `StatusMessages::Problems` is an optional `ProblemCounts` of
  errors and warnings, in place of the joined string.
  - The error glyph (`ICON_MDI_CLOSE_CIRCLE`) and the error count draw in the
    diagnostic error colour; the warning glyph (`ICON_MDI_ALERT`) and its count
    in the diagnostic warning colour. These are the Problems panel's glyphs.
  - A zero count is left out. With none at all, a check (`ICON_MDI_CHECK`)
    draws in the disabled-text colour.
  - The item's tooltip is the old wording, "2 errors, 5 warnings", from
    `ProblemCountText`.
- **Server health.** `ServerHealth` is `None`, `Starting`, `Ready` or `Failed`.
  - `ServerHealthOf` maps a server state. Starting and indexing are `Starting`.
    Ready is `Ready`. Failed, not found, not installed, a missing runtime and an
    incomplete project are `Failed`: the states `LanguageServices` already ranks
    as broken. Off, restricted and not configured are `None`.
  - `ChooseServerLine` gives each line the health of the server it speaks for.
    An install under way is `Starting`. An uninstall, and a line that speaks for
    no running server (no language support, restricted mode), are `None`.
- **The dot.** A filled circle 7 px across, centred on the text line, sits before
  the server's words. It is the git "added" colour (`GitPalette::GutterAdded`)
  when ready, the warning colour while starting and the error colour when
  failed. `None` draws no dot. The words keep their form, "C++: clangd ready",
  and their click ([0571](0571-choose-and-install-language-servers-from-settings-and-the-editor.md)).
- **Caret position.** "Ln 12, Col 5" draws in the code face at the interface
  size, `Core::PushCodeFont(Core::kInterfaceFontSize)`. Its width changes only
  when its number of digits does.
- **Gaps.** Items are `kStatusItemGap`, 16 px, apart, in place of spaces or a
  font size. The gap separates:
  - the branch from the change count;
  - the git segment from the path, and the path from the read-only warning;
  - each item on the right from the next, and the last from the bell.

  The distance from upstream stays one item spacing after the branch it
  qualifies. Inside an item, a glyph or the dot is 4 px from its text, and the
  error and warning counts are 8 px apart.
- **Hover.** Every item that takes a click fills the full height of the bar under
  the pointer: the branch, the change count, the server's words when they open
  settings, and the bell.
  - A text item's fill reaches 6 px past its text on each side, with square
    corners.
  - The fill is `Core::IconButtonFill`, the eased colours `DrawIconButton` uses.
  - Inert items take no fill.
  - The bell stays a `DrawIconButton`, with a third fit, `IconButtonFit::Bar`: a
    square one frame high, filled out to its corners.
- **Read-only warning.** "read-only: not valid text for its encoding" is an item
  of its own after the path, in the warning colour, only for such a document.
- **Colours.** `StatusMessages::Colours` carries the error and warning colours
  from `EditorPalette` and the ready colour from `GitPalette`. `GraphiteLayer`
  reads them from the active scheme each frame, so the built-in schemes and a
  custom one all colour the bar.
- **No wheel scroll.** The bar is begun with `ImGuiWindowFlags_NoScrollWithMouse`.
  An item one bar tall, with the window padding, reaches past the window's inner
  height, and a wheel would otherwise scroll the bar.
- **The path** stays. It leaves when the breadcrumb shows it above the code.

This amends the look of 0535 and 0571 and keeps their wording and their clicks.
It adds a fit to [0602](0602-draw-every-button-that-is-only-a-glyph-through-core-drawiconbutton.md).

## Rationale

A glyph in a colour is read at a glance where words have to be read; the words
stay beside the dot and in the problems' tooltip, so nothing is lost. The
proportional digits were what made the right-hand cluster jump, and drawing
digits that must not shift in the code face is already the rule. A fill the
height of the bar shows which pieces of a strip of text can be pressed, and
borrowing `DrawIconButton`'s fill keeps one hover look across the application.

## Consequences

### Positive

- Errors and warnings, and a ready, busy or broken server, are told apart by
  colour.
- The right-hand cluster stays put while the caret moves, until a number gains a
  digit.
- The pieces of the bar that take a click show it under the pointer.

### Negative

- The problem counts are no longer spelled out without hovering.
- An incomplete project shows the failed colour although its server answers.
- The caret position is in another face than the items beside it.
- An uninstall under way shows no dot.

### Neutral

- The bell's fill grows from one text line to the height of the bar.
- `Core::IconButtonFill` is public, so a pressable piece that is not a glyph
  button can share the fill.

## Alternatives Considered

- **Draw the bell as a status-bar item.** It would give the bell the bar's fill
  without touching Core, but it would be the one glyph-only button outside
  `DrawIconButton`, against 0602.
- **Hand the bar the server's state and colour it there.** An incomplete project
  could then show the warning colour, but the bar would learn the language
  services' states, which `ChooseServerLine` keeps from it.
- **Keep the counts spelled out, in colour.** "2 errors" reads without a glyph
  but takes several times the width of a glyph and a count.

## Related

- [0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md):
  every length here is scaled.
- [0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md): the
  rule that puts the caret position in the code face.
