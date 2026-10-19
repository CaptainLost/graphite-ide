# 0617. Draw file icons in a colour by file kind and folders in outline

Status: proposed
Date: 2026-10-19

## Context

In the Explorer, the solution view and Problems a file's icon was the first
character of its row's label, `icon + "  " + name`. It was drawn in the same
colour as the name, which under [0556](0556-show-git-status-in-the-file-tree-solution-view-and-editor-tabs.md)
is the file's git status colour. Changes, the review and the dialogs drew the
icon as text of its own, in the text or the disabled-text colour.

Folders were the filled `FOLDER` and `FOLDER_OPEN` glyphs in the text colour, so
they were brighter than the names beside them. `.h` and `.cpp` shared the C++
glyph.

A tab's icon is part of its window name, and a tab has one text colour. The
schemes have no key for file kinds, and the light scheme needs darker colours
than the dark one.

The marker column, where the status letter or dot stands, was as wide as an "M"
in the current face. It changed when [0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md)
moved the interface to Inter.

## Decision

Draw every file and folder icon in a list through `Graphite/src/UI/FileIcons.h`.

- **Kinds.** `KindOfFile` reads a file's kind from its extension, in any case.
  Each kind has a glyph and a colour for each scheme:

  | Kind | Extensions | Glyph | Dark | Light |
  |---|---|---|---|---|
  | `CppSource` | `.c` `.cc` `.cpp` `.cxx` | `LANGUAGE_CPP` | `#6AA7E6` | `#1E64B4` |
  | `Header` | `.h` `.hpp` `.hxx` | `ALPHA_H_BOX_OUTLINE` | `#B392E8` | `#7A46C8` |
  | `CSharp` | `.cs` | `LANGUAGE_CSHARP` | `#7FBF6A` | `#2E7D32` |
  | `Json` | `.json` | `CODE_JSON` | `#D8B25A` | `#9A7400` |
  | `Markdown` | `.md` | `LANGUAGE_MARKDOWN_OUTLINE` | `#8FA3B8` | `#55697E` |
  | `Lua` | `.lua` | `LANGUAGE_LUA` | `#6C8CE0` | `#2F4FB0` |
  | `Shell` | `.ps1` `.psm1` `.psd1` `.bat` `.cmd` | `POWERSHELL` | `#5FA9D6` | `#1F6F9E` |
  | `Project` | `.sln` `.slnx` `.vcxproj` `.csproj` | `MICROSOFT_VISUAL_STUDIO` | `#A77FE0` | `#6B3FB0` |
  | `Image` | `.png` `.jpg` `.jpeg` `.gif` `.bmp` `.ico` `.svg` `.webp` | `FILE_IMAGE_OUTLINE` | `#D0A060` | `#9A6A20` |
  | `Other` | anything else | `FILE_OUTLINE` | the dim folder colour | the dim folder colour |

- **Scheme.** The table is fixed in code, not in scheme keys. The scheme is light
  when the luminance of `ImGuiCol_WindowBg` is above 0.5. This is the test
  `Theme::Apply` makes, shared as `Core::IsLightBackground`.
- **Folders.** Folders use `FOLDER_OUTLINE` and `FOLDER_OPEN_OUTLINE` in the dim
  folder colour: the current text colour mixed 45 % toward the disabled-text
  colour, derived each time it is drawn.
- **Two draws.** A row draws its icon and its name separately:
  - The icon stands at the label's start, in its kind's colour.
  - The name starts `IconToNameOffset()` after it: the icon's width plus
    `kIconToNameGap`, 8 px scaled. The icon's width is the font size, which is
    every icon's advance.
  - The name keeps its own colour: the git status colour (0556) or the text
    colour.
  - In the Explorer and the solution view, the name's right edge, its ellipsis,
    the marker column and the open-tab mark stay where they were. A row there too
    narrow for the whole icon draws neither the icon nor the name.
- **Dimmed rows.** These are an ignored or unreadable row in the Explorer, and a
  missing project or file in the solution view.
  - The name is drawn in the disabled-text colour, as before.
  - The icon is drawn in its own colour at half its alpha.
  - The Explorer and a missing project draw such a row with the text colour set
    to the disabled one. There a folder's icon, or an `Other` file's, is the
    disabled-text colour at half its alpha.
- **Solution view.** The solution's own row takes the `Project` kind. A project's
  row takes the kind of the language it builds, through `KindOfProject`: `.vcxproj`
  is `CppSource` and `.csproj` is `CSharp`. Everywhere else a project file is a
  file like any other and takes `KindOfFile`.
- **Places.**
  - The Explorer: files and folders.
  - The solution view: the solution, solution folders, projects, project folders
    and files.
  - Changes: file and folder rows.
  - The review tree: folder rows. Its file rows carry no icon.
  - Problems: each file's heading.
  - The dialogs' file rows, `DialogLayout::FileRow`: the unsaved files on
    closing, and the checkout questions. There the icon was in the disabled-text
    colour.
- **Tabs.** A tab keeps its icon in its window name, in the tab's text colour.
  `Core::IconForExtension`, which the tabs use, gives `.h`, `.hpp` and `.hxx`
  `ALPHA_H_BOX_OUTLINE`. Its other glyphs are unchanged.
- **Marker column.** `RowMarkerColumnWidth` is 12 px scaled, not the width of an
  "M".
- **Project row id.** A project row keeps the ImGui id it had, its old label with
  the icon in it, so its open state stays where it was kept.

## Rationale

A colour for each kind lets the eye find a header, a project or a script in a
long list without reading extensions. Outline folders in a dim colour stop the
folders from outshining the files. Two draws let the icon carry the kind while
the name keeps the status. A fixed table that follows the scheme's base gives the
light scheme its darker colours without a scheme migration.

## Consequences

### Positive

- A header reads apart from its source, and one kind from another, in every
  list.
- Folders no longer outshine the names in them.
- The icon says what a file is and the name's colour says its status, on the same
  row.
- The marker column keeps its width whatever the face.

### Negative

- The kind colours are not part of a scheme, so a user scheme with an unusual
  window background can leave some of them hard to read.
- A tab's icon can differ from the list's. Tabs use `IconForExtension`, which
  keeps its own glyphs for these files:
  - `.c`: `LANGUAGE_C`;
  - `.md`: `LANGUAGE_MARKDOWN`;
  - `.py`: `LANGUAGE_PYTHON`;
  - project files: the C++ and C# glyphs;
  - the rest: `FILE`.
- In the lists, Python files lose their own glyph (they are `Other`), and C files
  draw the C++ glyph.
- An ignored folder's outline is fainter than its name, because the dim folder
  colour follows the dimmed text colour and is then halved.
- The same project file is violet in the Explorer, and in its language's colour
  as a project in the solution view.

### Neutral

- The icon's slot is 22 px at 100 %. Before, it was the icon plus two Inter spaces
  at 14 px, 21.875 px.
- In the tests every icon draws as ProggyClean's fallback glyph, so only its
  colour and its place are observed. Which glyph is drawn is checked through
  `GlyphOf` and `FolderGlyphOf`.

## Alternatives Considered

- **Scheme keys for the kind colours.** Every scheme would grow ten keys and their
  light pairs, for colours nobody has asked to edit. Making them editable stays a
  follow-up.
- **The icon in the status colour, as before.** The status already colours the
  name. One meaning on two pieces of the row leaves no room for the kind.
- **Projects in the `Project` kind in the solution view too.** A solution of C++
  and C# projects would show a column of identical violet rows. Telling C# and
  C++ projects apart was asked for before.
- **The dimmed icon in the disabled-text colour.** It would lose the kind. At half
  its alpha the kind still shows, faintly.

## Related

- [0556](0556-show-git-status-in-the-file-tree-solution-view-and-editor-tabs.md):
  the status colour the name keeps, and the marker column.
- [0603](0603-mark-whatever-opens-and-closes-in-a-list-with-the-one-dim-chevron.md):
  the dim chevron beside the folder icon.
- [0616](0616-draw-list-rows-taller-with-an-inset-rounded-highlight-in-the-accent.md):
  the rows the icons stand in.
- [0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md): the
  interface face whose icon advance sets the icon's width.
- [0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md):
  the scaling the gap and the marker column go through.
