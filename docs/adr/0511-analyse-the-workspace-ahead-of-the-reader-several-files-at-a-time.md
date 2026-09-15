# 0511. Analyse the workspace ahead of the reader, several files at a time

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

Walk the whole tree in the background and ask the server about a small, bounded
number of the files it serves at once, caching the answers so a file the pass
has already reached paints from cache in its first frame.

- **A small, bounded number at once, derived from the machine.** A `didOpen`
  pins a preamble and an AST in the server; a queue that held the whole tree
  open at once would cost one of each per file in it. The reason for a bound is
  therefore unchanged — it is what keeps the server's footprint independent of
  the size of the tree — but the bound is a quarter of the machine's hardware
  concurrency rather than one, because one file at a time left most of a modern
  machine idle while the pass ran for tens of minutes. The number is settable,
  precisely because a quarter is a judgement and not a measurement.
- **The bound above one carries an invariant that used to come free.** While
  one file was analysed at a time, two analyses of one path were impossible by
  construction; now they are impossible only because the pass checks for them
  before starting a file.
- **Yield to the document on screen.** While the active document has questions
  outstanding, nothing new starts, but an analysis already under way is
  finished rather than cancelled, because cancelling gives back none of the
  work the server has already done. The yield is itself bounded
  (`kForegroundYieldCapSeconds`), because a configured-but-absent server
  leaves the foreground's questions outstanding forever, and an unbounded
  yield would mean the pass never runs at all on such an install, silently.
- **Never close a document a tab has taken over.** `LanguageServices::
  CloseDocument` counts no references, and `DocumentWindows::LensFor` opens a
  document exactly once, so a background close after a tab takeover would
  leave that tab's document unknown to the server for the rest of its life.
  Each pending analysis records what the last tick told the server and disowns
  its `didOpen` instead of closing it. `PreviewSemantics` solves the same
  problem the same way, and predates this.
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
- **The answers outlive the session.** They go into `AnalysisCache` and into
  `AnalysisStore` together, so the second launch on an unchanged folder skips
  what the first one paid for. The store is a record of its own, [0513](0513-keep-the-workspace-analysis-on-disk-and-discard-it-on-any-doubt.md),
  because what it decides — an index so pruning is possible, discard on any
  doubt, a read on the UI thread — outlives this one.

## Rationale

The two prior mechanisms — starting servers early, keeping a closed tab's
answers — both wait for something else to happen before they help: a folder
opening, a tab reopening. Neither one asks a question on its own initiative.
Walking the tree is the only way to have an answer ready for a file nobody has
touched yet in this session, and bounding how much of it is in the server at
once is what keeps that walk affordable enough to run by default rather than as
a manual action someone has to remember to invoke. Where the bound sits is a
separate question from whether there is one: the bound is what makes the cost
independent of the tree, and the number is what decides how long the pass takes
to finish.

## Consequences

### Positive

- A file already visited by the pass paints its semantic colours and lens
  margin in its first frame instead of a moment later.
- The server's own memory use during the pass is bounded by the limit, not by
  how much of the tree has been walked.
- The mechanisms this reuses — disowning a `didOpen` instead of closing it,
  refusing a stale cache entry by stamp — already existed for other reasons,
  so the pass adds one more caller to each rather than a new idea to each.
- The second launch on an unchanged folder costs almost none of this: the store
  answers for every file whose stamp has not moved, so neither a server nor a
  worker is troubled for it.

### Negative

- Processor time is spent walking and parsing a tree nobody asked about, and
  running several files wide spends more of the machine on it at once — which
  is the point, and is also what somebody on a slower machine will feel.
- Disk is spent as well as processor time: the store grows a record per
  analysed file under the workspace, which 0513 costs out.
- A file clicked seconds after a folder is opened for the first time is not
  helped at all — the queue starts cold, and there has been no time for the
  walk to reach it. Only the store fixes that, and only from the second launch
  onwards.
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
no "recent files" to lean on, and the bound on how much is open at once is what
makes walking everything affordable, which removes most of the reason to guess
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

- [0512](0512-name-one-owner-for-every-document-opened-with-a-language-server.md) —
  the ownership rule this pass, as the third opener, has to keep.
- [0513](0513-keep-the-workspace-analysis-on-disk-and-discard-it-on-any-doubt.md) —
  the store the answers are kept in between sessions.
- `Graphite/src/Lang/Analysis/WorkspaceAnalysis.h` and `.cpp` — the queue, its
  concurrency limit, and `kForegroundYieldCapSeconds`.
- `Graphite/src/Settings/AnalysisSettings.h` — `AutomaticConcurrency` and the
  number someone can set instead of it.
- `Graphite/src/Editor/DocumentWindows.cpp` — `RestoreAnalysis`, `LensFor`, and
  the `PreviewSemantics` disown-on-takeover this record's pending analyses copy.
