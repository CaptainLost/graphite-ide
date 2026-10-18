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

- Load three faces through `Core::LoadFontSet`, in this order:
  1. the interface face, Inter Regular at `kInterfaceFontSize` (14 px);
  2. Material Design Icons merged onto it;
  3. the system symbol font (`seguisym.ttf`) merged onto it;
  4. the code face, JetBrains Mono Regular at `kCodeFontSize` (15 px);
  5. Material Design Icons merged onto it;
  6. the system symbol font merged onto it;
  7. the strong face, Inter SemiBold at 14 px, with nothing merged.

  The interface face is the atlas's first font and `io.FontDefault`.
- Both the interface and the code face carry the icons, each with a minimum
  advance of its own size and the same baseline lift, so an icon draws in either
  face. Both carry the system symbols.
- Pin the code face's symbol advance, minimum and maximum, to the advance of its
  `'0'`. The terminal draws a run of cells with one `AddText`, so a symbol wider
  than a cell would push the rest of its row off its cells. The interface face
  takes symbols at their own advance.
- `Core::PushInterfaceFont()` pushes the interface face at `kInterfaceFontSize`
  wherever it is called, including inside a code push; `Core::PushCodeFont()`
  pushes the code face at `kCodeFontSize`. Both sizes are unscaled base sizes,
  which ImGui scales by the interface scale and the monitor's DPI. Both are
  popped with `ImGui::PopFont()`.
- Code, cells and digits that must line up digit for digit draw in the code face.
  Everything else draws in the interface face.
- The strong face, which `ImGuiRenderer::GetBoldFont()` returns, is Inter
  SemiBold. `GetMonoFont()` returns the code face and `GetInterfaceFont()` the
  interface face.
- This amends 0590's face list: Inter Regular, Inter SemiBold and JetBrains Mono
  Regular are embedded, with Material Design Icons. JetBrains Mono Bold no longer
  is; its `.ttf` stays in `vendor/fonts`, as Italic does.

## Rationale

A proportional face fits a label in less width and reads as an interface, while a
monospaced face keeps code and cells in columns. Adding each face's merges right
after it, and the strong face after every merge, is what ImGui's merge-onto-last
rule requires. Two named sizes keep code at 15 px while the interface size moves
on its own.

## Consequences

### Positive

- Labels, menus, tabs and dialogs are narrower and read as an interface.
- Code views keep their face and their size.
- Icons and symbols draw in the interface and the code face alike.

### Negative

- The atlas holds Material Design Icons twice and reads `seguisym.ttf` twice,
  once per face, which costs memory and startup time.
- A number drawn in the interface face changes width as its digits change.
  Anything that must line up has to push the code face.

### Neutral

- The code face keeps `LegacySize` 15, so a one-argument `PushFont` of
  `GetMonoFont()` still pushes 15 px.
- The interface size is a starting value; the screen check may move it to 15 px.

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
