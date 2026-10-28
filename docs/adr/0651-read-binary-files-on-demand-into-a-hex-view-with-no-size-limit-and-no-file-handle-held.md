# 0651. Read binary files on demand into a hex view, with no size limit and no file handle held

Status: proposed
Date: 2026-10-28

## Context

- A binary file that names no image format, such as an executable, an archive or
  a dump, opened as a failure that said only `Binary file — N KB`.
- A text tab reads its whole file into a document, so text is capped at 512 MB
  (`MaximumFileSize`). A binary file worth looking at can be several gigabytes.
- A file held open by a view on Windows can keep git, a build or a log writer from
  replacing, renaming or deleting it, and a file deleted while a handle is open
  lingers until the handle closes.
- `Core::SyncWithFile` answers Unchanged for an item with no document, so a view
  of a file needs a disk stamp of its own.
- ImGui's scroll position is a float. A 5 GiB file has 335 million rows of 16
  bytes, and a float stops telling rows apart past 16 million.
- Copy and Select All are `FocusedEditor` commands that consume their chords even
  while disabled ([0597](0597-send-editor-commands-to-the-editor-view-that-has-the-keyboard.md)).
  Image tabs already route a focused tab's strokes through the text editor's
  routing, and give the status bar a view's own items
  ([0649](0649-open-images-as-views-inside-the-document-tabs.md),
  [0647](0647-let-the-view-on-screen-add-its-own-items-to-the-status-bar.md)).

## Decision

- **A hex item in `Core::DocumentSet`.** `ItemView` gains `Hex`. A file that
  probes as binary (a NUL in its first 8 KiB, not UTF-16) and whose extension
  names no image format loads as `View == Hex`, with no document and no failure,
  whatever its size: the probe sees the NUL before it looks at the size, so
  `MaximumFileSize` does not apply. A text file over 512 MB still fails as too
  large to open.
- **Kept in `DocumentWindows`, as image tabs are.** The same window loop draws
  it. Where the loop would draw the path row and the editor, an item with
  `View == Hex` and no failure calls `DrawHexView`
  (`Graphite/src/Hex/HexView.h`) with the path row and no tools, the editor's
  palette and the editor's code font size. A hex item whose file went missing
  draws the failure message, as a text tab does. `DocumentWindows` keeps one
  `HexViewState` (the selection and the top row) per tab path, and drops it when
  the tab closes.
- **Read on demand.** `HexFiles` (`Graphite/src/Hex/HexFiles.h`) holds one
  `HexFile` per open path. `GraphiteLayer` makes it with the shared job queue, and
  `DocumentWindows` holds it by reference. Every frame, the view asks for the rows
  on screen and for the selection's first four bytes. The file is read in 64 KiB
  blocks, one job a block, into a cache of the 64 blocks most recently asked for.
  `HexFiles::Update` takes the blocks that landed once a frame, before any tab
  draws. Rows not read yet show their offset and dim placeholders, a read still
  waiting after 0.2 s shows the loader at the header's right end, and a failed
  read shows its reason with Retry
  ([0633](0633-show-every-wait-with-the-core-loader-and-say-nothing-is-there-only-after-an-answer-said-so.md)).
- **No handle held.** Each read opens the file through `Core::ReadFileRange`,
  sharing read, write and delete with everyone else, and closes it before
  returning.
- **Changes on disk.** `HandleFilesChanged` and the check on regaining the focus
  ask the bytes to compare the file's modified time and size with what they last
  saw. On a change the blocks wanted are read again, and the old bytes stay on
  screen until the new ones land. The selection is clamped to the new length and
  the scroll is kept.
- **A 64-bit scroll.** The view keeps its top row as a 64-bit row number and
  draws a scrollbar of its own, in the look of
  [0612](0612-draw-scrollbars-as-a-grab-in-the-text-colour-over-no-track.md),
  mapping the grab to a row in double. ImGui's own scroll is not used for the rows.
- **Keys.** A hex tab whose window holds the keyboard routes the frame's strokes
  as `FocusedEditor` through `RouteFocusedEditorStrokes` before it draws, so Ctrl+W,
  Ctrl+F4, Ctrl+A and Ctrl+C reach their commands and the result is drawn in the
  same frame. The arrows, Page Up and Page Down, Home and End, and Ctrl+Home and
  Ctrl+End, which no command is bound to, are left over for the view. A popup open
  over the tab does not count as the tab holding the keyboard.
- **Copy and Select All.** With a hex tab active, the editor commands run on it at
  once rather than being queued for its next draw:
  - Select All selects every byte.
  - Copy reads the selected bytes from the file again, through `ReadFileRange` and
    not from the cache, and puts them on the clipboard as `4D 5A 90 00`. The chord
    copies silently; Copy from a menu reports `Copied`, as the editor does.
  - A selection of more than 1 MiB is refused before anything is read, with the
    warning toast `Select at most 1 MB to copy`.
  - In an empty file both are disabled. Cut, Paste, Toggle Line Comment, Undo and
    Redo are always disabled.
- **Status bar.** The items the view drew last frame, `Offset 0x…`, the count of
  bytes selected, the little-endian `u8`, `u16` and `u32` values and the file's
  size, are what `GetViewStatusItems` hands the bar while the tab is active, as a
  picture's are.
- **Restart.** `CaptureState` keeps a hex tab as `TabState{Path, Preview}` and may
  name it the active tab; `RestoreState` reopens it through the ordinary open
  path. The selection and the scroll are not kept.

### Images that cannot be shown

A later change fills this section.

## Rationale

Reading only the blocks on screen is what lets a file of any size open at once,
and opening the file for each read is what keeps the tab from ever standing in the
way of whoever writes it; the cost is one open a block, small beside the read. A
row number of its own is the only scroll that can reach the end of a file past a
few hundred megabytes. Keeping the hex tab one more kind of item gives it every
tab behaviour text and image tabs have, and routing its strokes through the
editor's routing keeps one place deciding which chords a focused view's commands
take.

## Consequences

### Positive

- A binary file of any size opens at once and shows its first rows as soon as one
  block is read.
- Git, a build or a log writer can replace, rename or delete a file shown as hex.
- A hex tab previews, closes, restores after a restart and carries its git dot as
  a text tab does.

### Negative

- Every block read opens and closes the file, so on a slow share each block waits
  for an open as well as for its bytes.
- Copy reads up to 1 MiB on the UI thread, a pause on a slow disk or share, and a
  larger selection cannot be copied at all.
- Each open hex tab may hold 4 MiB of cached blocks.
- `DocumentWindows` grows another injected dependency, another per-tab map and
  another branch in its window loop.
- A restored hex tab opens at the top with the first byte selected.

### Neutral

- The zoom chords and View → Zoom In change the editor's font in a hex tab, since
  the rows are drawn in it.
- The wheel over the rows scrolls them, with Ctrl held too, since the view takes
  the wheel and no text editor routes it.
- A hex tab whose file is missing draws the failure message and routes no strokes,
  so Ctrl+W does not close it, as for an image tab; File → Close Tab does.

## Alternatives Considered

### Read the whole file, or map it into memory

Rejected: reading it needs the memory of the file and brings back a size limit,
and a mapped view holds the file open, which stops a writer from truncating it and
leaves a deleted file pending until the tab closes.

### Keep one handle open for the life of the tab

Rejected: even with every sharing flag the open handle keeps a deleted file
lingering and the folder holding it from being removed, and the tab would have to
notice a replaced file through the old handle. One open a block costs little next
to the read.

### Scroll the rows with ImGui's own scroll and a list clipper

Rejected: the float scroll cannot address the rows of a multi-gigabyte file; past
16 million rows neighbouring rows share a scroll position, and the end of the file
cannot be reached.

### Copy from the cached blocks

Rejected: the cache holds at most 4 MiB, and only the rows that were wanted, so a
selection made by Select All reaches bytes never read; reading them through jobs
would make Copy finish frames later, after the user may have moved on.

## Related

- [0476](0476-load-a-tab-whose-file-appears-rather-than-only-clearing-its-missing-mark.md) —
  a tab whose file disappears and comes back.
- [0597](0597-send-editor-commands-to-the-editor-view-that-has-the-keyboard.md) —
  the editor commands Copy and Select All go through.
- [0621](0621-space-editor-lines-at-1-12-times-the-font-size.md) — the spacing of
  the rows.
- [0647](0647-let-the-view-on-screen-add-its-own-items-to-the-status-bar.md) — the
  status bar items the view fills.
- [0648](0648-let-a-tab-s-view-put-its-tools-on-the-right-of-the-path-row-and-give-the-row-to-image-tabs.md) —
  the path row, which a hex tab draws with no tools.
- [0649](0649-open-images-as-views-inside-the-document-tabs.md) — image tabs, the
  pattern hex tabs follow.
