# 0623. Show the file's path and the symbol at the caret above the code

Status: proposed
Date: 2026-10-19

## Context

- The document's path stood in the status bar, relative to the open folder,
  with a `*` before it while the document had unsaved changes. The status bar
  describes the document on screen, so with two documents side by side the path
  named only one of them, and it sat at the bottom of the window, far from the
  code it named. [0620](0620-show-problems-and-the-server-s-health-as-glyphs-and-colours-in-the-status-bar.md)
  kept it until it could be shown above the code.
- Nothing on screen said which function or type the caret was in. Since
  [0622](0622-keep-document-symbols-ranges-and-nesting-and-ask-for-them-again-after-each-save.md)
  each declaration holds its last line and its parent, and
  `InnermostDeclarationAt` gives the chain that contains a line, outermost
  first. The declarations move with edits between answers.
- A document window is drawn in the content colour
  ([0613](0613-draw-the-chrome-darker-than-the-documents-and-diffs-it-frames.md)),
  and the editor surface is a child window that fills what is left of it. The
  find bar is a child of that surface, drawn over its top. The information bars
  belong to the whole window, not to a document.
- File kinds have a glyph and a colour
  ([0617](0617-draw-file-icons-in-a-colour-by-file-kind-and-folders-in-outline.md)).

## Decision

- **The row.** Every tab with a text document gets one row above its editor
  surface: `DrawBreadcrumb(BuildBreadcrumb(...))`
  (`Graphite/src/Editor/Breadcrumb.h`). The row stands at the top of the
  window's inner rectangle (`ImGuiWindow::InnerRect.Min.y`), directly under the
  tab bar whatever height the tab bar has. It is not laid out at the cursor, so
  the window's top padding (8 px) is not kept above it; the padding still stands
  at the window's sides and bottom, and a tab with no row keeps all of it. It is
  `kBreadcrumbHeight` (24 px) tall and is laid out once, with no item spacing
  after it, so the editor surface starts at its bottom edge and runs to the
  bottom of the window's content. It is drawn on every frame,
  whether or not the document has symbols, so the code never moves when they
  arrive. The find bar, inside the surface, opens under it.
  - A tab with no text document, whose file could not be read, has no row. Every
    image opens as such a tab.
  - Diffs, review tabs, the results tab and the Settings document are drawn
    elsewhere and have no row.
- **The model.** `BuildBreadcrumb(root, path, declarations, caretLine)` is pure
  and copies what it needs into a `BreadcrumbModel` by value:
  - `Folders`: the folders between the open folder and the file. A file outside
    the open folder, or any file with no folder open, shows its absolute
    folders, its drive first (`D: › Temp › file.cpp`). A file with no folder
    shows its name alone.
  - `FileName` and its `FileKind`.
  - `Symbols`: the chain of functions and types that contain the caret's line,
    outermost first, each with its name and kind (`Renderer › Draw`). No server,
    no answer yet or no symbol at the caret gives the path alone.
  - `DocumentWindows` passes the open folder, the tab's path, the tab's
    edit-adjusted declarations and the caret's line at the start of the frame.
- **The look.** The interface face at `kBreadcrumbFontSize` (15 px), unscaled
  as `PushInterfaceFont` takes it, with the line centred in the row. Both are the
  mockup's, its 12 px text converted by Inter's factor
  ([0626](0626-size-fonts-by-the-em-the-mockup-was-drawn-at.md)).
  - The pieces are placed across the window's inner rectangle
    (`ImGuiWindow::InnerRect`), not its content region, so the first piece
    starts `kBreadcrumbLeftPadding` (12 px) from the window's left edge rather
    than 12 px inside its padding.
  - Folders are in the disabled-text colour.
  - The file name is in the text colour, after its kind's badge in its kind's
    colour
    ([0617](0617-draw-file-icons-in-a-colour-by-file-kind-and-folders-in-outline.md)).
  - Pieces are separated by `ICON_LC_CHEVRON_RIGHT`, 2 px from the pieces on
    each side, in the disabled-text colour at 0.7 alpha, so the separators read
    quieter than the folders.
  - Each symbol is its mark and then its name in the text colour. The mark is
    text in the code face at `kBreadcrumbMarkFontSize` (15 px, the mockup's
    11 px by JetBrains Mono's factor), centred in the row: `ƒ` (U+0192, which
    JetBrains Mono carries) for a function, method or constructor, and `{}` for
    a class, struct or interface. Both are purple: dark `#B392E8`, the
    mockup's, and light `#7A4FC4`, deeper for a light background, which the
    mockup does not draw.
  - A chevron is one font size wide. A mark is as wide as its text in the code
    face and stands 4 px before its name. The file's badge is its box wide,
    16 px scaled, and stands 4 px before the file name.
  - The row draws no background and no border. It sits on the document window's
    content colour, so it reads as part of the document.
- **Narrow rows.** The pieces fit in the window's inner width less 12 px on each
  side. When they do not, the leading folders go one at a time, and one `…` in
  the disabled-text colour stands where they were. The file name and the symbols
  always stay. When even they do not fit, the row is clipped at the window's
  inner right edge. `BreadcrumbWidth` and `FirstShownFolder` measure this.
- **Display only.** The row is an invisible button that never acts, pressed
  with `ImGuiButtonFlags_NoFocus | ImGuiButtonFlags_NoNavFocus`, and as wide as
  the window's content. A click on it moves no caret and focuses nothing: the
  editor keeps the keyboard if it had it, and does not take it if it had not.
- **The status bar.** The path item goes, and with it the `*` for unsaved
  changes, which the tab already shows. `DrawStatusBar` no longer takes the open
  folder. The read-only warning stays, in the warning colour, where the path
  stood.

This amends [0620](0620-show-problems-and-the-server-s-health-as-glyphs-and-colours-in-the-status-bar.md)
and [0535](0535-show-git-state-in-the-status-bar-read-only-and-keep-git-actions-in-the-panels.md).

## Rationale

A path above the code names the document it belongs to, in every pane at once,
and the symbol chain beside it answers "where am I" without a question to the
server per caret move, since the declarations are already held. A row that is
always there, and always the same height, keeps the code from jumping when a
document's symbols arrive or when the find bar opens. Dropping folders before
the file and the symbols keeps the part that changes with the caret on screen.
Seated at the top of the window's inner rectangle rather than at an offset of
its own, the row stays against the tab bar whatever height the tab bar is given,
and reads as the tab's second line rather than as a bar of its own.

## Consequences

### Positive

- Each pane names its own file and the function or type at its caret.
- The status bar's left side no longer grows with a long path, so it has room
  for its other items.
- A function typed since the last save appears once the save's answer arrives
  (0622).

### Negative

- Every document gives up 16 px of code: the row's 24 px less the 8 px of top
  padding it stands in.
- On a narrow pane a deep path shows only its last folders, where the status bar
  had the whole window's width for it.
- The row reads the caret at the start of the frame, before the editor handles
  input, so for one frame after the caret moves into another function the row
  still names the old one.
- The unsaved-changes `*` is gone from the status bar; only the tab marks it.
- The row's pieces look like they could be clicked and cannot.

### Neutral

- A tab whose file could not be read, an image among them, shows its message
  with no row above it. The status bar never showed such a tab's path either.
- An enum has no breadcrumb segment, because the server's enums are not kept as
  declarations (0622).

## Alternatives Considered

### Show the row only when the document has symbols

The path would show only in the status bar until a server answers. Rejected:
the code would move down by a row when the first answer arrived, and a plain
text file would never name itself.

### Draw the row inside the editor surface, over the first line

It would cost no height. Rejected: it would hide a line of code, scroll with
it or need its own layer, and the find bar already overlays the surface's top.

### Lay the row out as a plain `Dummy`

The smallest change. Rejected: a press on a window's empty space focuses that
window, so a click on the row took the keyboard from the editor below it.

### Give document windows no top padding

The row would start at the cursor as before. Rejected: ImGui's window padding is
one figure for the top and the bottom, so the editor surface would lose its
bottom padding too, and a tab with no row would set its message against the tab
bar.

### Keep the path in the status bar as well

Rejected: the same words twice, and the status bar's copy still named only the
document on screen.

## Related

- [0620](0620-show-problems-and-the-server-s-health-as-glyphs-and-colours-in-the-status-bar.md) —
  the status bar this takes the path from; amended here.
- [0535](0535-show-git-state-in-the-status-bar-read-only-and-keep-git-actions-in-the-panels.md) —
  the git segment the path stood beside; amended here.
- [0622](0622-keep-document-symbols-ranges-and-nesting-and-ask-for-them-again-after-each-save.md) —
  the ranges and nesting the symbol chain is read from.
- [0617](0617-draw-file-icons-in-a-colour-by-file-kind-and-folders-in-outline.md) —
  the file kind's badge and colour.
- [0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md) — the
  interface face.
- [0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md) —
  every length here is scaled.
- `Graphite/src/Editor/Breadcrumb.h` — `BuildBreadcrumb` and `DrawBreadcrumb`.
