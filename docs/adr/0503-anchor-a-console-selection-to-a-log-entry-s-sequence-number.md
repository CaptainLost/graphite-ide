# 0503. Anchor a console selection to a log entry's sequence number

Status: proposed
Date: 2026-08-23

## Context

The console panel drew every message with `ImGui::TextUnformatted`. That is a
draw call and nothing else: no item to hit, no selection, no menu. Text could
leave the panel only through the "Copy all" button, which takes the whole
buffer. A reader who wanted the three lines of a stack trace had to take four
thousand and cut the rest out somewhere else.

Making the sweep work needs a place in the console to hold on to — one for the
end the mouse pressed on and one for the end it dragged to. The obvious place
is the row: the console is a list, and a list is indexed. But the rows under a
selection do not stand still.

`RingBufferSink` keeps the last N messages and drops the oldest. When it
overflows, every row number names the message one further along than it named a
frame earlier — so a selection made at rest silently slides up the buffer while
new messages arrive, which is exactly when a console is being read. The filter
moves them too: typing into it renumbers every row below the first hidden one.

The messages themselves carried nothing that would survive either: a level, a
source and a string, none of them unique.

## Decision

`Core::LogEntry` carries a `Sequence`, a monotonic number `RingBufferSink`
stamps each message with as it arrives. It is never reset, `Clear()` included,
so a number handed out once is never handed out again.

A console selection is a pair of `ConsoleAnchor`s — a sequence number and a
column into that message. `ConsoleSelection` holds no messages of its own: the
entries are passed in when the text or the highlight is wanted, so the
selection cannot go stale against a buffer that has moved on.

The entries passed in are the ones the panel shows — the filter applied. A
message the reader cannot see contributes nothing to what they copy, and a
message the buffer has dropped contributes nothing either: both are simply
absent from the vector, and `ExtractText` skips whatever it does not find.

Columns count characters rather than bytes, converted through
`Core::ColumnOfByte` and `Core::ByteOfColumn`, so the sweep cuts a Polish
message where the reader sees it cut.

## Rationale

A row number answers "which line of the panel", and the question a selection
asks is "which message". Those two coincide only while nothing arrives, nothing
is dropped and nothing is filtered — that is, only while the console is not
being used as a console.

A sequence number costs eight bytes per entry and makes the difference between
the two questions unrepresentable: the anchor either finds its message or finds
nothing, and there is no third case where it finds a different one.

## Consequences

### Positive

- A selection survives new messages, a full buffer, and the filter being typed
  into, because none of those change what a message is called.
- The selection model is free of ImGui and is unit-tested directly, hit test
  included, through `ConsoleLayout` and `AnchorAtPoint` taking plain floats.
- Anything else that later wants to point at a console message — a jump from a
  build error, a bookmark — has a name to point with.

### Negative

- `Core::LogEntry` grows a field that only a reader with a selection needs, and
  every entry pays for it whether or not anything is selected.
- The panel keeps a second copy of the entries it shows, so a filtered console
  holds the messages twice.

### Neutral

- Sequence numbers are per-run. Nothing persists a selection across launches,
  and nothing should read one as an identity outside the process.

## Alternatives Considered

**Row indices, cleared whenever the row count changes.** Free, and wrong in the
common case: messages arrive while a console is read, so the selection would
disappear from under the reader every time anything was logged.

**Row indices, plus the count of messages dropped so far.** Enough to correct
for overflow, and no help at all against the filter, which reorders rows without
dropping anything. Two mechanisms for one question.

**A selectable widget per row.** `ImGui::Selectable` would give whole rows for
free, but a whole row is not what a reader wants out of a stack trace, and the
`Selectable` still has to be told which message it stands for — the same
question, unanswered.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) - why the sequence number is
  Core's and the selection built on it is the application's.
