# 0543. Load the settings file once and merge its changes into it at the end of a frame

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

The re-read had one virtue: every write merged with what was on disk. A key
edited by hand while the application ran — `languageServerBinary` has no
other way in — survived the next write, and two running instances did not
undo each other's changes.

The window's placement is stored in the same file. `Core::Application` needs
it to open the window, before any layer exists.

## Decision

- Make `AppSettings` an instance that reads its file once, in its constructor,
  and keeps the parsed document in memory, along with a snapshot of it.
- Getters read the document in memory. Setters change it and mark it
  unwritten. They never read the file.
- `Flush`, if anything is unwritten, reads the file once, sets on it every key
  whose value differs from the snapshot, removes every key the snapshot had
  and the document no longer has, and writes the result. The result becomes
  the document in memory, so a hand edit on disk is visible after the write,
  and the snapshot is retaken.
- When the read yields no keys at all - the file does not parse, or was
  deleted or emptied while the application ran - `Flush` writes the document
  in memory instead, since nothing on disk can be kept. Merging onto an empty
  read would write, and then adopt, only the keys this process changed.
- A failed write leaves the changes unwritten, so the next `Flush` tries
  again. It is logged once per run of failures.
- `GraphiteLayer` calls `Flush` at the end of every `OnImGuiRender`, so one
  frame's changes make one write, and again in `OnDetach` after `StateKeeper`
  flushes.
- `GraphiteApplication` owns the single instance, in a base listed before
  `Core::Application`. It reads the window placement from it while
  `Core::Application` is constructed, and the instance outlives the layers
  even when `Run` is left by an exception. It passes the instance by reference
  to `GraphiteLayer`. Every settings class, section and system that reads or
  writes a setting (`Workspace`, `ExplorerPanel`, `EditorSettings`,
  `SearchSettings`, `AnalysisSettings`, `ClaudeSettings`, `ActiveScheme`,
  `InterfaceSection`, `SchemeSection`) takes it by reference.
- Keep the reading rules as they were. A missing file, or one that does not
  parse, reads as empty and so gives every default. Nothing is written until
  a setter changes something. A file stamped by a newer version is never
  written (ADR 0531).

## Rationale

Reading once and parsing again only to write costs one parse per launch and
one read and one write per frame that changed something. Merging only the
keys this process changed keeps what the re-read used to keep. Ending the
frame with the flush, rather than flushing in each setter, merges changes made
in the same frame into one write. Passing the instance by reference lets a
test build one on a temporary file.

## Consequences

### Positive

- Starting the application parses `graphite.json` once.
- A setter is an assignment in memory, so a control can set a value on every
  frame without touching the disk more than once per frame.
- A hand edit made while the application runs survives, and is read into
  memory at the next write. Two instances keep each other's changes, except
  to a key both changed, where the later write wins.
- Tests construct `AppSettings` on a temporary path and read back what they
  wrote, including through `Load()` of the classes built on it.

### Negative

- A hand edit is only read back at the next write. Until then, and for a
  value already read into a class such as `EditorSettings` or the language
  server's binary at startup, the running application does not see it.
- A change made between frames, such as by a command run from an event, is
  written at the end of the next frame rather than at once. A crash in that
  window loses it.
- Nothing is flushed while the window is minimized, because no frame is drawn.
  A change made then waits for the window to be restored, or for the exit.
- A file that stops parsing, or is deleted or emptied, while the application
  runs is replaced by the document in memory at the next write, hand edits in
  it included.
- `SetKeyBindings` rewrites every `key.*` entry, so a binding added by another
  instance or by hand is dropped the next time this process edits a binding.
- A write that keeps failing is retried at the end of every frame that is
  drawn, one read and one write attempt each.
- `AppSettings` is not thread-safe. Every caller must stay on the main
  thread.

### Neutral

- The application owns the instance, not `GraphiteLayer`, because the window
  placement is needed before the layer exists.
- The test runner still redirects `UserDirectories` (ADR 0531). Other user
  files that are not yet instances still rely on it.

## Alternatives Considered

- **Write the document in memory as it is, without merging.** This is simpler
  and reads nothing at a write. But it silently drops every hand edit made
  while the application runs, and lets one instance revert everything another
  changed in the same session.
- **Write in each setter.** This loses nothing between frames. But it writes
  the file once per changed key, and once per frame for any control that sets
  a value as it is dragged.
- **Debounce through `StateKeeper`.** `StateKeeper` compares typed snapshots
  of the window and workspace state. Fitting every setting into one would
  mean copying the whole document every frame to find out whether it changed.
  A dirty flag answers that for nothing.
- **Re-read before each read.** This would show a hand edit at once. It also
  brings back a full parse for every value, which is the cost this record
  removes.
- **Let `GraphiteLayer` own the instance.** Then `Main.cpp` would need a
  second, one-off read of the file for the window placement.

## Related

- [0522](0522-persist-session-state-in-two-scopes-global-window-and-panels-per-workspace-tabs.md):
  `StateKeeper`, which now hands the window state to `AppSettings` instead of
  writing the file itself.
- [0531](0531-keep-per-user-state-in-the-user-s-profile-not-beside-the-executable.md):
  where `graphite.json` lives, and the version stamp `Flush` still honours.
