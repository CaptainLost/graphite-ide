# 0590. Keep a bounded change log in the text buffer for readers to catch up from

Status: proposed
Date: 2026-10-16

## Context

`TextBuffer::GetRevision()` is the id of the top undo entry. It goes back on undo, so it can say "changed" but not "what changed" or "newer". Every reader of a buffer either got told by the editor widget (`NotifyAfterEdit`), or recomputed from line 0 (after undo), or compared revisions as a backstop. A second view of the same document cannot be told by a widget it does not own.

## Decision

`TextBuffer` records every change: edits, undo, redo and reload alike. Each record holds a monotonic sequence number, the offset and lengths, the first line, the newlines removed and inserted, and the line count after the change.

`ReplaceContent` is one record marked `WholeContent`, and one undo step.

The log keeps between 1024 and 2048 records. A reader holds the last sequence it saw and calls `ChangesSince`; nullopt means "recompute".

`LinesChangedBy` and `ShiftOffset` turn a run of records into a line range and moved offsets. `GetRevision()` stays for "is it modified".

## Rationale

Pull, not push: a reader cannot miss an edit made by code that does not know about it. Measuring changed lines from both ends makes the order of a transaction's records irrelevant.

## Consequences

### Positive

- Views, syntax caches and future excerpt anchors share one mechanism.
- Undo re-lexes from its first changed line instead of line 0.

### Negative

- Every change costs a record plus two `ToPosition` lookups.
- A reader more than about 1024 changes behind recomputes everything.

### Neutral

- `WholeContent` readers clamp offsets instead of mapping them.

## Alternatives Considered

- **Push notifications to registered observers.** A reader is told exactly, but only by code that knows about it, and an observer that is never unregistered keeps a view alive. The editor widget's `NotifyAfterEdit` already shows how a missed call goes unnoticed.
- **A revision counter that only rises.** Cheap, and answers "newer", but still not "what changed", so every reader would recompute from the top.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md): the log names no view and no language.
