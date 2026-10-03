# 0550. Lay out sources by feature in whole-word folders mirrored by the tests

Status: proposed
Date: 2026-10-03

## Context

The source tree had grown without a rule for where a file goes. Four folders
were abbreviations — `Fs`, `Term`, `Sln`, `Lang` — in a codebase whose every
type is spelled out (`DirectoryWatcher`, `TerminalSession`, `SolutionReader`).
`Graphite/src/Panels` grouped files by kind rather than by feature: the views of
sessions, the terminal, search and the solution tree sat there while their
models lived in `Sessions`, `Search` and `Sln`, so a change to one feature
crossed two folders of fifty files each. `Git` did the opposite and kept models
and views together. `Editor` had 73 files at one level, `Git` 70, `Sessions` 46,
and the test projects were two flat folders of 99 and 149 files with no relation
to `src`.

## Decision

- **A folder is a feature, never a kind of file.** A panel, a view and the model
  behind it live in the same folder. There is no `Panels`, `Views` or `Models`
  folder. Pieces several features share go to `UI` (widgets and dialogs) or
  `Theme` (palettes built from the colour scheme).
- **Folder names are whole words.** `UI` and `ImGui` are the only abbreviations,
  being the established names of the thing.
- **A feature folder past about thirty files splits into subfolders by
  sub-feature** — `Editor/Hover`, `Git/History`, `Sessions/Transcript` — the way
  `Language` already split into `Analysis`, `Server` and `Syntax`. Files that
  belong to the feature as a whole stay at its root.
- **Tests mirror the source tree.** `<Name>Tests.cpp` sits at the same relative
  path under `tests/src` as `<Name>.h` does under `src`. Shared fixtures
  (`TempDirectory.h`, `TranscriptFixtures.h`, ...) stay at the root of
  `tests/src`, which both test projects put on their include path.
- Includes stay rooted at `src`: `"Editor/Hover/HoverState.h"`,
  `"Core/FileSystem/FileTree.h"`.

## Rationale

Grouping by feature puts what changes together in one place; the kind of a file
is already in its name (`*Panel`, `*View`, `*Model`) and does not need a folder
to say it again. A size threshold keeps a folder readable at a glance without
forcing subfolders onto features small enough not to need them.

## Consequences

### Positive

- One feature, one folder: working on session requests means `Sessions/Requests`.
- A test is found by the path of what it tests.

### Negative

- A move rewrites every include of the moved header, which touches many files
  and conflicts with any work in progress on them.
- Whether a model must stay free of ImGui is no longer visible from its folder;
  that boundary is held by review and by the headless test build, not the layout.

### Neutral

- Core stays flat: none of its folders passes the threshold.
- Debouncer, DeliveryInbox, GenerationSlot and LruResultCache are generic but stay
  in `Git`; moving them to `Core/Jobs` changes their namespace and is a separate
  decision.

## Alternatives Considered

- **Keep views apart from models** (`Panels` beside feature folders), only tidied.
  Lost because it splits every feature across two folders and `Git` had already
  shown that keeping them together works.
- **Subfolders by kind inside each feature** (`Sessions/Views`, `Sessions/Model`).
  Lost for the same reason at a smaller scale: a sub-feature such as requests
  would again be spread over two folders.

## Related

- [0479](0479-settle-one-house-style-for-naming-formatting-and-layout.md) — house
  style; its paths predate this layout and are kept as written.
- [0384](0384-sf-20-use-namespaces-to-express-logical-structure.md) — one
  namespace per project, so a folder never implies a namespace.
