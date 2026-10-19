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

The approved mockup draws a file's kind as a short text badge, such as `C++`,
`H`, `MD` or `{}`, in the kind's colour, and a folder as a grey outline. Lucide,
the icon set of [0625](0625-draw-the-interface-s-icons-from-lucide.md), has no
icons for languages or file formats, so a glyph per kind tells C, C++, Lua and
Python apart only by colour.

A tab's icon is part of its window name, and a tab has one text colour. The
schemes have no key for file kinds, and the light scheme needs darker colours
than the dark one.

The marker column, where the status letter or dot stands, was as wide as an "M"
in the current face. It changed when [0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md)
moved the interface to Inter.

## Decision

Draw every file and folder icon in a list through `Graphite/src/UI/FileIcons.h`.
A file's icon is its kind's badge; a folder's is a Lucide glyph.

- **Kinds.** `KindOfFile` reads a file's kind from its extension, in any case.
  Each kind has a badge, `BadgeOf`, a tab glyph, `GlyphOf`, and a colour for
  each scheme, `ColourOf`. The badges are the mockup's table; the rows it lacks
  are marked "app".

  | Kind | Extensions | Badge | Tab glyph | Dark | Light |
  |---|---|---|---|---|---|
  | `CppSource` | `.cpp` `.cc` `.cxx` | `C++` | `file-code` | `#6AA7E6` | `#1E64B4` |
  | `CSource` (app) | `.c` | `C` | `file-code` | `#6AA7E6` | `#1E64B4` |
  | `Header` | `.h` `.hpp` `.hxx` `.inl` `.ipp` | `H` | `file-code-2` | `#B392E8` | `#7A46C8` |
  | `CSharp` (app) | `.cs` | `C#` | `hash` | `#7FBF6A` | `#2E7D32` |
  | `Json` | `.json` | `{}` | `file-json` | `#D8B25A` | `#9A7400` |
  | `Markdown` | `.md` | `MD` | `file-text` | `#8FA3B8` | `#55697E` |
  | `Lua` | `.lua` | `LUA` | `file-code` | `#6C8CE0` | `#2F4FB0` |
  | `PowerShell` | `.ps1` `.psm1` `.psd1` | `PS` | `terminal` | `#5FA9D6` | `#1F6F9E` |
  | `Batch` | `.bat` `.cmd` | `BAT` | `terminal` | `#5FA9D6` | `#1F6F9E` |
  | `Solution` | `.sln` `.slnx` | `SLN` | `app-window` | `#A77FE0` | `#6B3FB0` |
  | `CppProject` | `.vcxproj` | `VCX` | `app-window` | `#A77FE0` | `#6B3FB0` |
  | `CSharpProject` (app) | `.csproj` | `CSP` | `app-window` | `#A77FE0` | `#6B3FB0` |
  | `Image` | `.png` `.jpg` `.jpeg` `.gif` `.bmp` `.ico` `.webp` | `IMG` | `file-image` | `#D0A060` | `#9A6A20` |
  | `VectorImage` | `.svg` | `SVG` | `file-image` | `#D0A060` | `#9A6A20` |
  | `Python` (app) | `.py` | `PY` | `file` | disabled text | disabled text |
  | `PlainText` | `.txt` | `TXT` | `file` | disabled text | disabled text |
  | `Ini` | `.ini` | `INI` | `file` | disabled text | disabled text |
  | `ResourceScript` | `.rc` | `RC` | `file` | disabled text | disabled text |
  | `Other` | anything else, `.filters` among them | `≡` (U+2261) | `file` | disabled text | disabled text |

  - A kind the mockup lacks takes the nearest short text. A C# project is
    `CSP`, the first three letters of its extension, as the mockup makes
    `.vcxproj` `VCX`. C takes the C++ colour, and Python the dim colour it had
    as `Other`.
  - The dim kinds take the current disabled-text colour, which is the mockup's
    dim colour, read each time they are drawn.
- **Badge.** A badge is its text, centred in a box 17 × 15 px scaled, with no
  fill and no border. The box is the mockup's 16 × 14 scaled by 14/13. It
  stands at the label's start, centred on the line.
  - The text is in the badge face, JetBrains Mono Bold at `Core::kBadgeFontSize`
    (9 px, the mockup's 8 px by 14/13) through `ImGuiRenderer::GetBadgeFont()`,
    in the kind's colour.
  - Letters are 0.3 px scaled closer than their advance, as the mockup's
    letter spacing is. ImGui has none, so each character is drawn on its own,
    and `AddText` snaps each one's start to a whole pixel.
  - Without the font set loaded, as in the tests, the badge draws in the current
    face.
- **Scheme.** The table is fixed in code, not in scheme keys. The scheme is light
  when the luminance of `ImGuiCol_WindowBg` is above 0.5. This is the test
  `Theme::Apply` makes, shared as `Core::IsLightBackground`.
- **Folders.** Folders use `folder` and `folder-open` at 16 px scaled, the
  mockup's 15 px, centred on the line, in the folder grey: the current text
  colour mixed 77 % toward the disabled-text colour, derived each time it is
  drawn.
  - Under the dark scheme that is `#9096A1`, a step from the mockup's
    `#8F96A1`.
  - Under the light scheme it is `#5D5D5D`, between the text's `#242424` and the
    disabled text's `#6E6E6E`.
- **Two draws.** A row draws its icon and its name separately:
  - The icon stands at the label's start, in its kind's colour.
  - The name starts `IconToNameOffset()` after it: the icon's slot plus
    `kIconToNameGap`, 5 px scaled. The slot, `FileIconWidth()`, is the badge's
    box, 17 px scaled, the widest icon. That puts the name 22 px after the
    label's start at 100 %.
  - The name keeps its own colour: the git status colour (0556) or the text
    colour.
  - In the Explorer and the solution view, the name's right edge, its ellipsis,
    the marker column and the open-tab mark stay where they were. A row there too
    narrow for the whole icon draws neither the icon nor the name.
  - A tree row's note, such as the solution's project count, is shown only when
    three font sizes of the name fit beside it past the icon's slot.
- **Dimmed rows.** These are an ignored or unreadable row in the Explorer, and a
  missing project or file in the solution view.
  - The name is drawn in the disabled-text colour, as before.
  - The icon, badge or folder, is drawn in its own colour at half its alpha.
  - The Explorer and a missing project draw such a row with the text colour set
    to the disabled one. There a folder's icon is the disabled-text colour at
    half its alpha, and so is a dim kind's badge.
- **Solution view.** The solution's row and every project's row take their
  file's kind through `KindOfFile`. The solution is `SLN`, a C++ project `VCX`
  and a C# project `CSP`, all three violet.
- **Places.**
  - The Explorer: files and folders.
  - The solution view: the solution, solution folders, projects, project folders
    and files.
  - Changes: file and folder rows. The icon column is the badge's box, and a
    file's name follows its badge by `IconToNameOffset()`. A folder row measures
    its glyph's ink at the folder size, and keeps the badge-to-name gap between
    the folder, its chevron and its name.
  - The review tree: folder rows. Its file rows carry no icon.
  - Problems: each file's heading.
  - The dialogs' file rows, `DialogLayout::FileRow`: the unsaved files on
    closing, and the checkout questions. There the icon was in the disabled-text
    colour.
  - The breadcrumb's file
    ([0623](0623-show-the-file-s-path-and-the-symbol-at-the-caret-above-the-code.md)).
  - Settings, under Language Servers: each language's entry shows the badge of
    its language's source files, `C++` or `C#`, in place of a glyph. A language
    without a badge keeps `braces`. A page offers its badge through
    `SettingsPage::GetBadge`.
- **Tabs.** A document's or a diff's tab takes its kind's tab glyph,
  `GlyphOf(KindOfFile(path))`. A window name is text, which cannot hold a
  badge's box, so a tab shows the glyph, not the badge, in the tab's text colour.
  - The glyph stands before the `###`. ImGui identifies a window by what follows
    `###`, so window ids and `imgui.ini` keys do not change.
  - A tab that draws the badge itself must not prefix the glyph as well, as
    `GlyphOf`'s warning says.
  - `Core::IconForExtension` gives `.h`, `.hpp` and `.hxx` `file-code-2`.
    No Graphite code calls it any more.
- **Marker column.** `RowMarkerColumnWidth` is 12 px scaled, not the width of an
  "M".
- **Project row id.** A project row's ImGui id is `##` and its path, as a file
  row's is. A tree row's open state lives in ImGui's storage for the session, not
  in `imgui.ini`.

## Rationale

A colour for each kind lets the eye find a header, a project or a script in a
long list without reading extensions. A short text badge names what no outline
glyph can, a language or a format, and it is what the mockup draws. Outline
folders in a grey between the text and the dim text stop the folders from
outshining the files. Two draws let the icon carry the kind while the name keeps
the status. A fixed table that follows the scheme's base gives the light scheme
its darker colours without a scheme migration.

## Consequences

### Positive

- A header reads apart from its source, and one kind from another, in every
  list, by its badge as well as its colour.
- C, C++, Lua, Python, C# and a C# project read apart from a C++ one, which a
  glyph per kind could not do.
- Folders no longer outshine the names in them.
- The icon says what a file is and the name's colour says its status, on the same
  row.
- The marker column keeps its width whatever the face.

### Negative

- The kind colours are not part of a scheme, so a user scheme with an unusual
  window background can leave some of them hard to read.
- ImGui sizes a face by its ascender-to-descender height, so JetBrains Mono at
  9 px has an em of about 6.8 px, where the mockup's 8 px em is 8.6 px scaled.
  The badges are a fifth smaller in their box than the mockup's.
- The letter spacing is snapped to whole pixels a character at a time, so it is
  not exactly the mockup's.
- A tab shows a generic glyph in the tab's text colour, not the badge or the
  kind's colour that the list shows.
- An ignored folder's outline is fainter than its name, because the folder grey
  follows the dimmed text colour and is then halved.
- A folder row in Changes sets its name closer to the folder and the chevron
  than before, by the badge-to-name gap of 5 px.

### Neutral

- The name's offset is 22 px at 100 %, as it was, but now it does not grow with
  a pushed font size: the slot and the gap are fixed lengths, scaled.
- The atlas holds JetBrains Mono Bold for ASCII and `≡` only.
- In the tests every badge draws in ProggyClean, the face a test context has, so
  only its colour, its place and its character count are observed. Which text is
  drawn is checked through `BadgeOf`, and that the badge face has every badge's
  characters is checked by loading the font set.

## Alternatives Considered

- **A Lucide glyph per kind.** Lucide has no language or format icons, so C, C++,
  Lua and Python would share `file-code` and read apart only by colour.
- **A filled or outlined badge.** The mockup draws neither, and a box beside every
  name would outweigh the names.
- **The code face drawn twice, a pixel apart, for bold.** A doubled stroke is a
  whole pixel wider, more than a stem is at 9 px, where the vendored Bold costs
  0.2 MB in the executable.
- **Scheme keys for the kind colours.** Every scheme would grow a key per kind and
  its light pair, for colours nobody has asked to edit. Making them editable stays a
  follow-up.
- **The icon in the status colour, as before.** The status already colours the
  name. One meaning on two pieces of the row leaves no room for the kind.
- **Project rows in the colour of the language they build.** C# and C++ projects
  would read apart, but the approved table and the mockup make project files
  violet, and their badges already tell them apart.
- **Tabs keep `IconForExtension`'s glyphs.** A tab and the row of the same file
  would show different glyphs, for C, Markdown, Python, image, script and project
  files.
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
  faces, the badge face among them.
- [0625](0625-draw-the-interface-s-icons-from-lucide.md): the folder glyphs and the
  tab glyphs.
- [0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md):
  the scaling the box, the gap and the marker column go through.
