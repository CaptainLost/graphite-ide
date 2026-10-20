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

The mockup the interface is drawn to sets the bar's words in `#aab1bb`, quieter
than the text colour, brings a pressable item up to the text colour under the
pointer, pads each item's fill by 7 px and draws a ready server's dot in
`#7fbf6a`.

## Decision

- **Size.** The bar is `kStatusBarHeight`, 23 px scaled and rounded to a whole
  pixel, not the height of a frame, and draws in the interface face at
  `kStatusBarFontSize`, 15 px, the mockup's 12 px converted by Inter's factor
  ([0626](0626-size-fonts-by-the-em-the-mockup-was-drawn-at.md)). Every item's
  line is centred in the bar.
- **Tooltips.** The bar's 15 px font and its frame padding stay pushed while
  its items are drawn, and a tooltip takes the font in effect where it is
  begun. So the item under the pointer leaves its tooltip with the bar, which
  shows it once they are popped, at the interface size every other tooltip is
  drawn at. An item's tooltip shows at once, as before. The bell draws its
  `DrawIconButton` without a tooltip and leaves "Notifications" the same way
  once the pointer has rested, as an icon button's own tooltip waits.
- **Problems.** `StatusMessages::Problems` is an optional `ProblemCounts` of
  errors and warnings, in place of the joined string.
  - The error glyph (`ICON_LC_CIRCLE_X`) and the error count draw in the
    diagnostic error colour; the warning glyph (`ICON_LC_TRIANGLE_ALERT`) and its count
    in the diagnostic warning colour. These are the Problems panel's glyphs.
  - A zero count is left out. With none at all, a check (`ICON_LC_CHECK`)
    draws in the disabled-text colour.
  - The item's tooltip is the old wording, "2 errors, 5 warnings".
    `GraphiteLayer` builds it with `ProblemCountText` and passes it as
    `StatusMessages::ProblemsDetail`, so the bar includes nothing of the
    language services.
- **Server health.** `ServerHealth` (`Editor/StatusBar/ServerHealth.h`) is
  `None`, `Starting`, `Ready`, `Limited` or `Failed`.
  - `ServerHealthOf` maps a server state. Starting and indexing are `Starting`.
    Ready is `Ready`. An incomplete project is `Limited`: its server answers,
    and `LspAnswerRouter` and the workspace analysis count it as ready, but it
    sees only part of the project. Failed, not found, not installed and a
    missing runtime are `Failed`. Off, restricted and not configured are `None`.
  - `ChooseServerLine` gives each line the health of the server it speaks for.
    An install under way is `Starting`. An uninstall, and a line that speaks for
    no running server (no language support, restricted mode), are `None`.
- **Text colour.** The bar's own words are the status grey, `StatusBarTextColour`:
  the text colour 47 % of the way to the disabled text, `#aab1bb` in the dark
  scheme.
  - They are the branch, the distance, the change count, the server's words,
    the background pass, the caret position, the encoding, the line ending and a
    centred message. The bell's glyph is the status grey too.
  - The dim branch of a folder without a repository, the dim check, the problem
    counts, the dot and the read-only warning keep their own colours.
- **The dot.** A filled circle 7 px across, centred on the text line, sits before
  the server's words.
  - When ready it is `ServerReadyColour`: the mockup's `#7fbf6a` on a dark
    chrome. On a light one it is `#206020`, the light git added colour
    (40, 120, 40) made as much darker as the dark green is brighter than the
    dark git added colour, a quarter.
  - It is the warning colour while starting or limited, and the error colour
    when failed. `None` draws no dot.
  - The words keep their form, "C++: clangd ready", and their click
    ([0571](0571-choose-and-install-language-servers-from-settings-and-the-editor.md)).
- **Caret position.** "Ln 12, Col 5" draws in the code face at the bar's size,
  `Core::PushCodeFont(kStatusBarFontSize)`. Its width changes only when its
  number of digits does.
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
  - A text item's fill reaches 7 px past its text on each side, with square
    corners.
  - The fill is `Core::IconButtonFill`, the eased colours `DrawIconButton` uses.
  - As the fill fades in, the item's words go from the status grey to the text
    colour. The bell's glyph does too: the bell rests dim
    (`IconButtonRest::Dim`), with the status grey pushed as the disabled-text
    colour, and a dim icon button eases its glyph from that colour to the text
    colour with its fill.
  - Inert items take no fill.
  - The bell stays a `DrawIconButton`, with a third fit, `IconButtonFit::Bar`: a
    square one frame high, filled out to its corners. The bar sets the frame
    padding so that a frame is as tall as the bar, whatever padding frames have
    elsewhere.
- **Read-only warning.** "read-only: not valid text for its encoding" is an item
  of its own after the path, in the warning colour, only for such a document.
- **Colours.** `StatusMessages::Colours` carries the error and warning colours
  from `EditorPalette` and the ready colour from `ServerReadyColour` for the
  scheme's chrome. `GraphiteLayer` reads them from the active scheme each frame,
  so the built-in schemes and a custom one all colour the bar; a custom scheme
  sets the ready dot only through whether its chrome is light.
- **No wheel scroll.** The bar is begun with `ImGuiWindowFlags_NoScrollWithMouse`.
  An item one bar tall, with the window padding, reaches past the window's inner
  height, and a wheel would otherwise scroll the bar.
- **The path** stays. It leaves when the breadcrumb shows it above the code.

This amends the look of 0535 and 0571 and keeps their wording and their clicks.
It adds a fit to [0602](0602-draw-every-button-that-is-only-a-glyph-through-core-drawiconbutton.md).

Amended by [0623](0623-show-the-file-s-path-and-the-symbol-at-the-caret-above-the-code.md):
the path has left the status bar for the breadcrumb row above the code. The
read-only warning stays, where the path stood.

## Rationale

A glyph in a colour is read at a glance where words have to be read; the words
stay beside the dot and in the problems' tooltip, so nothing is lost. The
proportional digits were what made the right-hand cluster jump, and drawing
digits that must not shift in the code face is already the rule. A fill the
height of the bar shows which pieces of a strip of text can be pressed, and
borrowing `DrawIconButton`'s fill keeps one hover look across the application.
The status grey keeps the bar quieter than the code, and taking it from the text
colours gives the light scheme the same relation without a value of its own.

## Consequences

### Positive

- Errors and warnings, and a ready, busy or broken server, are told apart by
  colour.
- The right-hand cluster stays put while the caret moves, until a number gains a
  digit.
- The pieces of the bar that take a click show it under the pointer, in their
  fill and in their words.

### Negative

- The problem counts are no longer spelled out without hovering.
- The warning colour means two things, a server still starting and one with an
  incomplete project; only the words tell them apart.
- The caret position is in another face than the items beside it.
- An uninstall under way shows no dot.
- A custom scheme no longer chooses the ready dot's colour.

### Neutral

- The bell's fill grows from one text line to the height of the bar.
- `Core::IconButtonFill` is public, so a pressable piece that is not a glyph
  button can share the fill.

## Alternatives Considered

- **Draw the bell as a status-bar item.** It would give the bell the bar's fill
  without touching Core, but it would be the one glyph-only button outside
  `DrawIconButton`, against 0602.
- **Hand the bar the server's state and colour it there.** It would need no
  health value of its own, but the bar would learn the language services'
  states, which `ChooseServerLine` keeps from it.
- **Keep the counts spelled out, in colour.** "2 errors" reads without a glyph
  but takes several times the width of a glyph and a count.
- **Keep the git added colour for the ready dot.** It follows a custom scheme,
  but at `#6a9955` it is darker than the mockup's dot.
- **Add a scheme colour for the ready dot.** A custom scheme could set it, but
  every scheme and the Appearance page would gain a colour for one 7 px dot.

## Related

- [0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md):
  every length here is scaled.
- [0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md): the
  rule that puts the caret position in the code face.
