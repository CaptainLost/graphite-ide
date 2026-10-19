# 0626. Size fonts by the em the mockup was drawn at

Status: proposed
Date: 2026-10-19

## Context

- The mockup the visual refresh follows draws interface text in Inter at 13 px
  and code in JetBrains Mono at 13 px, and gives every length in CSS pixels.
- The refresh took the app's interface size, Inter at 14 px
  ([0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md)), as
  the mockup's 13 px, and scaled every mockup length by 14/13 to match.
- ImGui and CSS mean different things by a font's size:
  - ImGui sizes a face by its ascender-to-descender height. stb_truetype's
    `stbtt_ScaleForPixelHeight` divides the size asked for by the face's hhea
    ascent less its descent.
  - CSS sizes a face by its em.
  - Inter's ascent less its descent is 1.21 em, and JetBrains Mono's is 1.32 em.
- So Inter at 14 px had an em of 11.6 px against the mockup's 13. The editor's
  default code size, 13 px, had an em of 9.9 px against the mockup's 13.
- Measured at 100 %, capitals stood 8 px tall in the interface against 9 px in
  the mockup, and 8 px in the code against 10 px. The 14/13 factor rested on the
  two ems being equal, which they were not.
- The reader asked for the sizes as in the mockup.

## Decision

- **Fonts.** Convert a font size from the mockup's CSS pixels to ImGui's by the
  face's ascent less its descent in ems, rounded to a whole pixel: 1.21 for Inter
  and 1.32 for JetBrains Mono.

  | Text | Mockup | Face | ImGui size |
  |---|---|---|---|
  | Interface, `Core::kInterfaceFontSize` | 13 px | Inter, and Inter SemiBold for the strong face | 16 px |
  | Breadcrumb, `kBreadcrumbFontSize` | 12 px | Inter | 15 px |
  | Status bar, `kStatusBarFontSize` | 12 px | Inter | 15 px |
  | Lens labels | 11 px | Inter | 13 px, 0.78 of the editor's size |
  | File badges, `Core::kBadgeFontSize` | 8 px | JetBrains Mono Bold | 11 px |
  | Code, `Core::kCodeFontSize` | 13 px | JetBrains Mono | 17 px |

- **Editor.** The editor's default size is `Core::kCodeFontSize`, 17 px. A line
  is `round(1.12 × font size)` tall, 19 px at 17 px, which is the mockup's line
  ([0621](0621-space-editor-lines-at-1-12-times-the-font-size.md)).
- **Lengths.** Take every mockup length one to one: a length of N CSS pixels is N
  pixels at 100 %, through `Core::Scaled`. There is no 14/13 factor.

  | What | Length | Record |
  |---|---|---|
  | List row | 22 px, a 16 px text line and 6 px | [0616](0616-draw-list-rows-taller-with-an-inset-rounded-highlight-in-the-accent.md) |
  | Panel header | 36 px | [0618](0618-head-a-panel-with-one-row-of-quiet-choices-and-icon-actions.md) |
  | Breadcrumb row | 24 px | [0623](0623-show-the-file-s-path-and-the-symbol-at-the-caret-above-the-code.md) |
  | Title bar | 28 px | this record |
  | Window buttons | 42 px wide | this record |
  | Status bar | 23 px | [0620](0620-show-problems-and-the-server-s-health-as-glyphs-and-colours-in-the-status-bar.md) |
  | Tree indent, `IndentSpacing` | 14 px | — |
  | Badge box | 16 × 14 px | [0617](0617-draw-file-icons-in-a-colour-by-file-kind-and-folders-in-outline.md) |
  | Folder glyph | 15 px | [0617](0617-draw-file-icons-in-a-colour-by-file-kind-and-folders-in-outline.md) |
  | Tool strip | 36 px wide, buttons 26 px, rounded 6 px, 6 px apart | [0610](0610-toggle-tool-panels-from-a-strip-that-ignores-where-they-are-docked.md), [0614](0614-mark-an-open-tool-panel-with-an-accent-bar-at-the-strip-s-edge.md) |

  The badge keeps its 5 px gap to the name, so a name starts 21 px after the
  label's start. The strip keeps its groups, its separators and its open bar,
  2 px wide and 5 px short of the button at each end.
- **List bodies.** A 22 px row holds its 16 px text centred only while the item
  spacing is at most half the row spacing, 3 px, and the interface's is 5 px.
  Each list lays its rows out inside a `ListBodyScope`, which caps the spacing
  for the list alone, and a popup begun from a row takes the interface's
  spacing back through a `ListPopupScope` (0616). Menus, forms and dialogs keep
  5 px.
- **Status bar.** The bar is pinned at its own height, rounded to a whole pixel,
  not the height of a frame. It draws in the interface face at its own size, and
  sets the frame padding so that a frame is as tall as the bar, which keeps the
  bell, a frame-high icon button, filling the bar.

## Rationale

The mockup is drawn in ems, so a size taken over unconverted draws every glyph
smaller than the mockup's, by a tenth in the interface and a quarter in code,
whatever the lengths around it do. Converting
each face by its own ascent less its descent makes the capitals as tall as the
mockup's, and once the text matches, nothing is left for a factor on the lengths
to make up, so each length is the mockup's own number.

## Consequences

### Positive

- At 100 % the interface's and the code's capitals are as tall as the mockup's,
  about 9.5 px, within half a pixel.
- A mockup length or font size can be read off the mockup and written down with
  one rule per face, with no factor to remember.

### Negative

- Everything is bigger than it was: the interface text by two pixels, code views
  that take the code face's own size (the console, the terminal, the diff view,
  search hits and the conversation view) from 15 px to 17 px, and an editor left
  at its default from 13 px to 17 px. Fewer lines and rows fit on screen.
- The factors belong to the font files. A different face needs its own factor,
  and a size converted with another face's factor comes out wrong.
- Rounding to a whole pixel leaves each size within half a pixel of the mockup's
  em: Inter's 13 px is 15.7 px converted, drawn at 16.
- A tooltip asked for inside the status bar draws at the bar's 15 px.
- Items inside a list that are not rows are spaced 3 px, where the rest of the
  interface spaces them 5 px.

### Neutral

- An editor size someone has set is kept: zoom still starts from the stored
  size, and only an editor at the default changes.
- A hash drawn in the code face inside a chrome row still takes the interface
  size (0615), so the row keeps its pitch.

## Alternatives Considered

- **Keep ImGui sizes equal to the CSS sizes and scale lengths by 14/13.** This was
  the rule before. It lost because the text stayed smaller than the mockup's
  while the boxes round it grew.
- **Scale each face's rasteriser through `ImFontConfig::ExtraSizeScale`, so that a
  size of 13 draws a 13 px em.** ImGui would still lay a line out at the size
  asked for, 13 px, while the glyphs reach 15.7 px from ascender to descender, so
  text would overflow its own line.
- **Raise the interface scale by 1.21.** Every length would grow with the text
  and leave the mockup's.
- **FreeType instead of stb_truetype.** ImGui's FreeType loader asks for the
  same real dimension, ascender to descender, so the sizes would not change, and
  it is another dependency.

## Related

- [0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md),
  [0616](0616-draw-list-rows-taller-with-an-inset-rounded-highlight-in-the-accent.md),
  [0617](0617-draw-file-icons-in-a-colour-by-file-kind-and-folders-in-outline.md),
  [0618](0618-head-a-panel-with-one-row-of-quiet-choices-and-icon-actions.md),
  [0620](0620-show-problems-and-the-server-s-health-as-glyphs-and-colours-in-the-status-bar.md),
  [0621](0621-space-editor-lines-at-1-12-times-the-font-size.md),
  [0623](0623-show-the-file-s-path-and-the-symbol-at-the-caret-above-the-code.md),
  [0625](0625-draw-the-interface-s-icons-from-lucide.md),
  [0610](0610-toggle-tool-panels-from-a-strip-that-ignores-where-they-are-docked.md)
  and [0614](0614-mark-an-open-tool-panel-with-an-accent-bar-at-the-strip-s-edge.md):
  edited in place to these sizes.
- [0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md):
  the interface scale every length is multiplied by.
