# 0543. Load the settings file once and write it from memory at the end of a frame

Status: proposed
Date: 2026-10-01

## Context

`AppSettings` was a class of static functions. Every getter loaded and parsed
`graphite.json` to answer one value, and every setter loaded it, changed one
key and wrote the whole file back. Starting the application parsed the file
more than fifteen times. A setter was costly enough that the font-size slider
was written only on release. Because the class was a global, a test that
reached a setter wrote whatever file `UserDirectories` named, and the suite
had to redirect the process-wide `UserDirectories` before any test ran.

The window's placement is stored in the same file. `Core::Application` needs
it to open the window, before any layer exists.

## Decision

- Make `AppSettings` an instance that reads its file once, in its constructor,
  and keeps the parsed document in memory.
- Getters read the document in memory. Setters change it and mark it
  unwritten. They never read the file.
- `Flush` writes the whole document if anything is unwritten. `GraphiteLayer`
  calls it at the end of every `OnImGuiRender`, so one frame's changes make
  one write, and again in `OnDetach` after `StateKeeper` flushes.
- `GraphiteApplication` owns the single instance. It reads the window
  placement from it, then passes it by reference to `GraphiteLayer`. Every
  settings class, section and system that reads or writes a setting
  (`Workspace`, `ExplorerPanel`, `EditorSettings`, `SearchSettings`,
  `AnalysisSettings`, `ClaudeSettings`, `ActiveScheme`, `InterfaceSection`,
  `SchemeSection`) takes it by reference.
- Keep the reading rules as they were. A missing file, or one that does not
  parse, reads as empty and so gives every default. Nothing is written until
  a setter changes something. A file stamped by a newer version is never
  written (ADR 0531).

## Rationale

Reading once and writing on change costs one parse per launch and one write
per change. That is the least the settings need. Ending the frame with the
flush, rather than flushing in each setter, merges changes made in the same
frame into one write. It still writes before the
next frame. Passing the instance by reference lets a test build one on a
temporary file.

## Consequences

### Positive

- Starting the application parses `graphite.json` once.
- A setter is an assignment in memory, so a control can set a value on every
  frame without touching the disk more than once per frame.
- Tests construct `AppSettings` on a temporary path and read back what they
  wrote, including through `Load()` of the classes built on it.

### Negative

- The file is not merged with what is on disk. A key added to `graphite.json`
  by hand while the application runs is lost at the next write. That includes
  the hand-edited `languageServerBinary`, so edit the file with the
  application closed. A second instance of the application writing the same
  file now replaces the first instance's values instead of merging them.
- A change made between frames, such as by a command run from an event, is
  written at the end of the next frame rather than at once. A crash in that
  window loses it.
- `AppSettings` is not thread-safe. Every caller must stay on the main
  thread.

### Neutral

- The application owns the instance, not `GraphiteLayer`, because the window
  placement is needed before the layer exists.
- The test runner still redirects `UserDirectories` (ADR 0531). Other user
  files that are not yet instances still rely on it.

## Alternatives Considered

- **Write in each setter, without re-reading.** This is simpler and loses
  nothing between frames. But it writes the file once per changed key, and
  once per frame for any control that sets a value as it is dragged.
- **Debounce through `StateKeeper`.** `StateKeeper` compares typed snapshots
  of the window and workspace state. Fitting every setting into a snapshot
  would mean copying the whole document every frame to find out whether it
  changed. A dirty flag answers that for nothing.
- **Re-read before each write, merging what is on disk.** This keeps a hand
  edit made while the application runs. It also brings back a full parse for
  every change, which is the cost this record removes.
- **Let `GraphiteLayer` own the instance.** Then `Main.cpp` would need a
  second, one-off read of the file for the window placement.

## Related

- [0522](0522-persist-session-state-in-two-scopes-global-window-and-panels-per-workspace-tabs.md):
  `StateKeeper`, which now hands the window state to `AppSettings` instead of
  writing the file itself.
- [0531](0531-keep-per-user-state-in-the-user-s-profile-not-beside-the-executable.md):
  where `graphite.json` lives, and the version stamp `Flush` still honours.
