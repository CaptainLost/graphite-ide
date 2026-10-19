# 0615. Draw the interface in Inter and code in JetBrains Mono

Status: proposed
Date: 2026-10-18

## Context

Every piece of text was drawn in JetBrains Mono at 15 px, with JetBrains Mono
Bold for toast and notification titles and settings headings. A monospaced face
keeps code, terminal cells and diffs in columns, but on labels, menus, tabs and
dialogs it is wide and makes the chrome read as a terminal. ImGui merges a font
onto whichever face was added to the atlas last, so the order in which faces and
their merges are added decides which face draws icons and symbols. Inter's
digits are proportional, and ImGui cannot turn on its tabular figures (`tnum`).

## Decision

- Load four faces through `Core::LoadFontSet`, in this order:
  1. the interface face, Inter Regular at `kInterfaceFontSize` (16 px);
  2. the Lucide icons merged onto it;
  3. the system symbol font (`seguisym.ttf`) merged onto it;
  4. the code face, JetBrains Mono Regular at `kCodeFontSize` (17 px);
  5. the Lucide icons merged onto it;
  6. the system symbol font merged onto it;
  7. the strong face, Inter SemiBold at 16 px, with the Lucide icons merged onto
     it;
  8. the badge face, JetBrains Mono Bold at `kBadgeFontSize` (11 px), which draws
     the file kinds' badges of
     [0617](0617-draw-file-icons-in-a-colour-by-file-kind-and-folders-in-outline.md).

  The interface face is the atlas's first font and `io.FontDefault`.
- **Sizes.** A size is ImGui's, which measures a face from its ascender to its
  descender, where CSS measures it by its em. Inter's ascender-to-descender is
  1.21 em and JetBrains Mono's 1.32 em, so the sizes above are the mockup's CSS
  sizes times those factors, rounded: the interface's 13 px is 16 px, the
  code's 13 px is 17 px and the badges' 8 px is 11 px
  ([0626](0626-size-fonts-by-the-em-the-mockup-was-drawn-at.md)).
- The badge face holds only ASCII and the identical-to sign, U+2261. ImGui's
  dynamic atlas reads a face's glyph ranges only to preload, so every other
  character is shut out through `GlyphExcludeRanges`. It carries no icons and no
  symbols, and comes last, after every merge.
- The interface, the code and the strong face carry the icons, each with a
  minimum advance of its own size and lowered by its own descent
  ([0625](0625-draw-the-interface-s-icons-from-lucide.md)), so an icon draws in
  any of them; the code face keeps JetBrains Mono's Powerline glyphs over the
  icons at their codepoints. The interface and the code face carry the system
  symbols.
- Pin the code face's symbol advance, minimum and maximum, to the advance of its
  `'0'`. The terminal draws a run of cells with one `AddText`, so a symbol wider
  than a cell would push the rest of its row off its cells. The interface face
  takes symbols at their own advance.
- `Core::PushInterfaceFont(float size = kInterfaceFontSize)` pushes the
  interface face and `Core::PushCodeFont(float size = kCodeFontSize)` pushes the
  code face, each at the size it is given, wherever it is called, including
  inside a push of the other face. The default sizes are the two named sizes,
  16 px and 17 px. Every size passed is an unscaled base size, which ImGui scales
  by the interface scale and the monitor's DPI. Both are popped with
  `ImGui::PopFont()`.
- Code, cells and digits that must line up digit for digit draw in the code face.
  Everything else draws in the interface face. Within that rule:
  - A hash shown inside a chrome row, in the history list, the commit details
    and the review, draws in the code face at the interface size, so every row
    keeps its pitch.
  - Search and reference hit rows draw wholly in the code face, so each is one
    code line (17 px) plus spacing, a pixel taller than a chrome list row at
    100 %.
  - In the conversation view, an opened tool step's edit lines, input JSON and
    result are code.
  - Lens labels draw in the interface face at 0.78 of the editor's font size
    ([0621](0621-space-editor-lines-at-1-12-times-the-font-size.md)).
- The strong face, which `ImGuiRenderer::GetBoldFont()` returns, is Inter
  SemiBold. `GetMonoFont()` returns the code face, `GetInterfaceFont()` the
  interface face and `GetBadgeFont()` the badge face.
- This amends 0590's face list: Inter Regular, Inter SemiBold, JetBrains Mono
  Regular and JetBrains Mono Bold are embedded, with the Lucide icons. Bold
  draws only the badges. JetBrains Mono Italic stays in `vendor/fonts`,
  not embedded.

## Rationale

A proportional face fits a label in less width and reads as an interface, while a
monospaced face keeps code and cells in columns. Adding each face's merges right
after it, and the strong face after every merge, is what ImGui's merge-onto-last
rule requires. Two named sizes keep code at 17 px while the interface size moves
on its own.

## Consequences

### Positive

- Labels, menus, tabs and dialogs are narrower and read as an interface.
- Code views keep their face and their size.
- Icons draw in every face, and symbols in the interface and the code face
  alike.

### Negative

- The atlas holds the Lucide icons three times and reads `seguisym.ttf` twice,
  once per face, which costs memory and startup time.
- A number drawn in the interface face changes width as its digits change.
  Anything that must line up has to push the code face.

### Neutral

- The two sizes are the mockup's 13 px, each converted by its own face's factor
  (0626), so they differ although the mockup draws both at one size.

## Alternatives Considered

- **IBM Plex Sans.** This lost in the mockups, which showed it beside Inter and
  where Inter was chosen.
- **Keeping JetBrains Mono everywhere, direction C of the mockups.** This lost
  because a monospaced face on every label is what made the chrome read as a
  terminal, and the reader did not choose that direction.
- **A variable Inter file.** This lost because ImGui reads a variable font only at
  its default instance, so it cannot give SemiBold.

## Related

- 0534, 0590, 0606
