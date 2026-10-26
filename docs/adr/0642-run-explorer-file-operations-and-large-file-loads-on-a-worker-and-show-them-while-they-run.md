# 0642. Run Explorer file operations and large file loads on a worker and show them while they run

Status: proposed
Date: 2026-10-26

## Context

Three operations ran synchronously on the UI thread and froze the window for
seconds. Measured in Release:

| Operation | Size | Time |
|---|---|---|
| Duplicate a folder | 5,000 files | 1.3 s |
| Duplicate a folder | 20,000 files | 5.5 s |
| Delete to the Recycle Bin | 5,000 files | 0.47 s |
| Delete to the Recycle Bin | 20,000 files | 1.5 s |
| Open a file | 100 MiB | 0.74 s |
| Open a file | 300 MiB | 2.1 s |

The delete ran inside its confirmation dialog's frame, so the dialog stayed
frozen on screen until it finished.

[0633](0633-show-every-wait-with-the-core-loader-and-say-nothing-is-there-only-after-an-answer-said-so.md)
asks that every wait be visible. A frozen window shows nothing at all.

## Decision

### Duplicate and Delete in the Explorer

They run as jobs.

- The confirmation dialog closes at once.
- While a job runs, its row shows the loader.
- The status bar says what is happening, for example "Duplicating Foo… 1,234
  files" or "Moving Foo to the Recycle Bin…".
  - Duplicate counts the files it has copied.
  - Delete to the Recycle Bin can report no progress, so it shows no count.
- Actions on that path are disabled until the job ends.
- Success refreshes the tree, as before. A failure is reported in the toast
  already used for that operation.

### Files above 4 MiB

They load in a job.

- The tab opens at once, showing the loader with the file's name and size.
- The large-file question (above 16 MiB) is still asked before the load starts.
- The document becomes an ordinary open document when its text lands. Only then
  do language servers, syntax colouring and the other consumers see it.
- Closing the tab drops the result.
- Opening the same file twice shares one load.
- A failed read shows its reason in the tab, with Retry.

Smaller files keep the synchronous path. They open in a few milliseconds, and
code that assumes an open document has text stays simple.

## Rationale

These are the only measured operations whose cost grows without bound with the
user's data, and none of them must hold the frame. A threshold keeps the common
open path as it is.

## Consequences

### Positive

- Large copies, deletes and files no longer freeze the window, and each shows
  what it is doing.

### Negative

- A tab can now exist without text. Every consumer that walks open documents
  must skip a loading one.
- An Explorer path can be busy, which disables actions on it.

### Neutral

- Copies and deletes still cannot be cancelled.

## Alternatives Considered

- **A modal progress window.** It blocks the rest of the application for work
  that touches one path.
- **Load every file in a job.** It would touch every consumer for files that
  already open in a frame.

## Related

- [0633](0633-show-every-wait-with-the-core-loader-and-say-nothing-is-there-only-after-an-answer-said-so.md),
  [0643](0643-preview-only-files-that-open-without-asking.md)
