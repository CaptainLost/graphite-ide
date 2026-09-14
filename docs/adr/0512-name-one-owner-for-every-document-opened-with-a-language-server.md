# 0512. Name one owner for every document opened with a language server

Status: proposed
Date: 2026-09-14

## Context

`LanguageServices::OpenDocument` remembers a file's text in a map —
`ManagedServer::OpenDocuments`, keyed by path — and forwards a `didOpen` to the
client. `CloseDocument` erases the entry and forwards a `didClose`. The map is
a `std::map<path, SentDocument>`, so a second `didOpen` for a path overwrites
the remembered text rather than adding to it, and the first `didClose` removes
the file entirely however many callers believe they have it open. There is no
reference count.

The map exists for restarts, not for ownership: when a server process dies,
`RestartServer` replays every entry to the new one, which is the only way a
document the editor still has on screen survives a crash. Counting references
was never needed while `DocumentWindows` was the only thing that opened a
document, because a tab is the only object with a lifetime to count.

That stopped being true twice. `PreviewSemantics` opens a document for a file a
preview pane is showing without a tab, and says in its own comments that
refcounting `OpenDocuments` is out of scope. `WorkspaceAnalysis` (ADR 0511)
then became the third opener, asking the server about files nobody has opened
at all. Three openers, one un-counted map entry, and each of the two bugs the
whole-branch review called Critical was the same bug: one subsystem sent a
`didClose` for a document another one was still asking about, or a second
`didOpen` overwrote the text another one had remembered.

## Decision

Every `didOpen` sent through `LanguageServices` has exactly one owner, and the
rule that owner follows is: **close only what you opened, and only while nobody
else holds it.** Before opening or closing a document, a subsystem must be able
to answer whether somebody else already holds that path — not whether a tab
does, which is a narrower question and the one that was wrong.

The three owners, and how each keeps the rule:

- **`DocumentWindows`** owns the document behind a tab. `LensFor` opens it once
  when the tab first needs a lens, `PruneLensStates` closes it when the tab
  goes. It is the strongest claim: everything else defers to it.
- **`PreviewSemantics`** owns the document behind a preview pane, but only when
  no tab does. `Entry::Opened` records whether this class sent the `didOpen`;
  on the transition to a tab holding the file it sets `Opened` to false and
  disowns rather than closes, and on the transition back it opens again.
  `Holds` exposes that flag so a third party can ask whether this class is the
  holder.
- **`WorkspaceAnalysis`** owns a document only for the length of one flight, and
  only for a file nothing else holds. It asks the `heldElsewhere` predicate
  before requesting a file's text, again when the text arrives — the read is a
  round trip through a worker, and a tab can open in between — and records the
  answer on the flight every tick, because the four places a flight is torn
  down include two that have no predicate to ask with. When the answer is true
  it disowns its `didOpen` exactly as `PreviewSemantics` does.

The predicate `GraphiteLayer` hands down is therefore the union of the holders,
not `m_Documents.Find(path)`. A preview pane holds no `DocumentSet` entry, so a
tab check alone reports a file as free while a pane is still asking about it.

## Rationale

The rule is written down because its absence is not visible at any single call
site. Each of the three subsystems is correct when read alone; the damage only
appears when two of them name the same path, and the map they share has no way
to say so. Naming the owner makes "may I close this?" a question with an answer
in the code rather than an assumption about which subsystems exist — which is
what failed when the third one was added.

## Consequences

### Positive

- A subsystem that wants to open a document has one question to answer, and
  `GraphiteLayer` has one place that answers it for everybody.
- Adding a fourth opener means extending the holder predicate and implementing
  the disown transition — a known shape with two working examples, rather than
  a rediscovery of the same Critical bug.
- No change to `LanguageServices`, so restart replay, diagnostics versioning
  and the existing document lifetime keep working exactly as they do.

### Negative

- The rule is a convention with no mechanism behind it. Nothing in
  `LanguageServices` refuses a second `didOpen` or an unowned `didClose`, so a
  future caller that ignores this record is not stopped, only wrong.
- The holder predicate lives in `GraphiteLayer` and has to be updated whenever
  a new opener appears. Forgetting to is silent: the pass simply steals a
  document from the new subsystem.
- A redundant `didOpen` is still sent when a tab opens over a file a preview
  pane already holds. Harmless today — the text matches — but it is the same
  gap that made the disk-text overwrite possible.

### Neutral

- Disowning rather than closing leaks at most one document entry in the window
  between a tick and a `Clear` in the same frame. That is deliberate, and the
  harmless direction: a leaked entry costs the server one open file until it
  restarts, while the other direction takes a document away from a reader that
  will never open it again.

## Alternatives Considered

### Reference-count `LanguageServices::OpenDocuments`

The real fix, and the one this record defers rather than rejects. A count
beside each `SentDocument`, incremented by `OpenDocument` and decremented by
`CloseDocument`, would make every one of these bugs impossible by construction
and remove the disown transitions from both `PreviewSemantics` and
`WorkspaceAnalysis`.

Not taken now because the count alone is not the change. `OpenDocument` also
remembers the text, and three owners holding one entry disagree about what that
text is: a tab's unsaved buffer, a preview's snapshot, and the bytes on disk are
three different things, and whichever `didOpen` arrived last is what
`RestartServer` replays. Doing this properly means deciding whose text wins —
which is a question about document ownership in the editor, not about a map —
and the branch that surfaced the problem is not the place to answer it. The
rule above makes the current three correct; the count is what makes the fourth
safe without reading this record.

### Give each opener its own key into `OpenDocuments`

Key the map by (path, opener) so the three never collide. Rejected because the
collision is not an accident of keying: the server holds one document per URI,
and three entries for one path would mean three `didOpen` notifications for it,
which is exactly what the protocol does not allow.

### Let only `DocumentWindows` open documents, and have the others ask through it

Tempting — one owner, no rule to remember. Rejected because a preview pane and
a background pass would then each need a document object they have no other use
for, and `WorkspaceAnalysis` would acquire a dependency on the editor it was
deliberately built without (it takes a port and a predicate precisely so it can
be tested with neither).

## Related

- [0511](0511-analyse-the-workspace-ahead-of-the-reader-one-file-at-a-time.md) —
  the background pass's own policy, which this record is the ownership rule
  beneath.
- `Graphite/src/Lang/Server/LanguageServices.cpp` — `OpenDocument`,
  `CloseDocument` and `RestartServer`, the map and the replay.
- `Graphite/src/Editor/PreviewSemantics.h` — `Entry::Opened` and `Holds`, the
  first implementation of the rule.
