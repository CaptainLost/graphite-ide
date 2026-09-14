# 0511. Analyse the workspace ahead of the reader, one file at a time

Status: proposed
Date: 2026-09-14

## Context

Opening a file showed the lexer's colours at once and the language server's
semantic colours a moment later — a visible recolour, and a lens margin blank
until `documentSymbol` answered. `LanguageServices::WarmUp` already removed
part of this by starting servers when a folder opens rather than when a
document does, and `AnalysisCache` already kept a closed tab's answers ready
for its reopening. Neither helped the file being opened for the first time in
a session, which is the common case: the server has never been asked about it,
so there is nothing to have kept.

## Decision

Walk the whole tree in the background and ask the server about every file it
serves, one file at a time, caching the answers so a file the pass has already
reached paints from cache in its first frame.

- **One flight at a time.** A `didOpen` pins a preamble and an AST in the
  server; a queue that held many files open at once would cost many of them.
  Holding exactly one keeps the server's footprint independent of the size of
  the tree — the price is processor time, not memory.
- **Yield to the document on screen.** While the active document has questions
  outstanding, nothing new launches, but a flight already in the air is
  finished rather than cancelled, because cancelling gives back none of the
  work the server has already done. The yield is itself bounded
  (`kForegroundYieldCapSeconds`), because a configured-but-absent server
  leaves the foreground's questions outstanding forever, and an unbounded
  yield would mean the pass never runs at all on such an install, silently.
- **Never close a document a tab has taken over.** `LanguageServices::
  CloseDocument` counts no references, and `DocumentWindows::LensFor` opens a
  document exactly once, so a background close after a tab takeover would
  leave that tab's document unknown to the server for the rest of its life.
  The flight records what the last tick told the server and disowns its
  `didOpen` instead of closing it. `PreviewSemantics` solves the same problem
  the same way, and predates this.
- **The cache budgets in bytes, not entries.** Sixteen entries was sized for
  someone moving between a handful of files; a whole-tree pass would evict its
  own work within seconds. Bytes are also the only unit a settings panel can
  honestly offer someone who does not know how large their files are.
- **A cached entry is a first frame, never a source of truth.** The stamp is
  the file's modification time, and a stale entry is refused rather than
  drawn. Saving a header changes the semantics of everything that includes it
  without changing their own stamps, so cached colours can be briefly out of
  date until the server answers — the same trade `DocumentWindows::
  RestoreAnalysis` already makes for a reopened tab.

## Rationale

The two prior mechanisms — starting servers early, keeping a closed tab's
answers — both wait for something else to happen before they help: a folder
opening, a tab reopening. Neither one asks a question on its own initiative.
Walking the tree is the only way to have an answer ready for a file nobody has
touched yet in this session, and doing it one file at a time is what keeps
that walk affordable enough to run by default rather than as a manual action
someone has to remember to invoke.

## Consequences

### Positive

- A file already visited by the pass paints its semantic colours and lens
  margin in its first frame instead of a moment later.
- The server's own memory use during the pass is bounded by one open document,
  not by how much of the tree has been walked.
- The mechanisms this reuses — disowning a `didOpen` instead of closing it,
  refusing a stale cache entry by stamp — already existed for other reasons,
  so the pass adds one more caller to each rather than a new idea to each.

### Negative

- Processor time is spent walking and parsing a tree nobody asked about, for
  minutes on a small repository and tens of minutes on a large one.
- A file clicked seconds after a folder opens is not helped at all — the
  queue starts cold, and there has been no time for the walk to reach it.
- The feature is a setting precisely because the two costs above are real, and
  a slower machine or a larger tree is a fair reason to decline them.

### Neutral

- The yield cap means a foreground document that never gets an answer — a
  configured-but-absent server — does not permanently starve the background
  pass, but it also means the pass can start competing with the foreground for
  the server's attention after ten seconds of silence. This is accepted rather
  than tuned further, absent a case where it matters in practice.

## Alternatives Considered

### Prefetch only a likely-next set — explorer siblings, recent files, a header's source counterpart

Cheaper, and usually right about what gets opened next. Rejected because the
whole tree was the deliberate target: a project opened for the first time has
no "recent files" to lean on, and the one-flight-at-a-time rule is what makes
walking everything affordable, which removes most of the reason to guess
narrower.

### Keep the server's copy of every file open at once

The literal reading of "analyse everything," and unaffordable — hundreds of
preambles and ASTs held in the server simultaneously, for a benefit ("already
open" tabs) most of the tree will never need.

### Re-queue a file when it changes on disk

`DirectoryWatcher` reports changed directories, not individual files, so
reacting to it precisely would need a second mechanism to find which file
inside a changed directory actually moved. The stamp-based refusal already
makes a stale cache entry unusable rather than silently wrong, so an unrefreshed
entry costs nothing beyond a first frame's worth of visibly-late colour — not
enough to justify the extra mechanism.

## Related

- `Graphite/src/Lang/Analysis/WorkspaceAnalysis.h` and `.cpp` — the walk, the
  one-flight queue, and `kForegroundYieldCapSeconds`.
- `Graphite/src/Editor/DocumentWindows.cpp` — `RestoreAnalysis`, `LensFor`, and
  the `PreviewSemantics` disown-on-takeover this record's flight copies.
