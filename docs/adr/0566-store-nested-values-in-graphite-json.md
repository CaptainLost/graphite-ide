# 0566. Store nested values in graphite.json

Status: proposed
Date: 2026-10-09

## Context

`graphite.json` held only top-level strings, with dotted keys (`claude.price.<model>`, `key.<id>`) standing in for hierarchy. A language server's `initialization_options` and `settings` are objects of a shape Graphite does not know and passes through, and its arguments are a list.

## Decision

`graphite.json` may hold nested objects and arrays. `Core::JsonFile` reads, writes and removes a value at a path of keys and lists every leaf, which is any value that is not an object with members. `AppSettings`' merge-on-flush (0543) compares and writes leaf by leaf, removing first and setting second, and never removes a path that still has a leaf beneath it. Flat keys stay as they are; settings that belong together go in objects: `languages.<Language>.language_server`, `lsp.<server>.binary.path`, `lsp.<server>.binary.arguments`, `lsp.<server>.initialization_options`, `lsp.<server>.settings`.

## Rationale

A server's documented settings can be pasted as they are, and a value of unknown depth cannot be flattened into keys without knowing its shape.

## Consequences

### Positive

Two instances changing different servers keep each other's change, and a hand edit inside an object survives a flush.

### Negative

The file mixes two styles, flat dotted keys and nested objects. A concurrent edit beneath a leaf that this process turned into an object is lost.

### Neutral

`settingsVersion` stays: an older build reads a nested value as an empty string, never changes it, and so never writes it back differently.

## Alternatives Considered

- Flat dotted keys for everything (`lsp.roslyn.settings.csharp|inlay_hints...`) lose because the values are objects of arbitrary depth that the user copies from the server's documentation.
- A file per server (`lsp/<id>.json`) loses because it is a second file to find, back up and merge.

## Related

- [0543](0543-load-the-settings-file-once-and-merge-its-changes-into-it-at-the-end-of-a-frame.md):
  the merge-on-flush that now works leaf by leaf.
- [0531](0531-keep-per-user-state-in-the-user-s-profile-not-beside-the-executable.md):
  where `graphite.json` lives and its version stamp.
- The spec `2026-10-09-language-servers-design.md`, which is not tracked.
