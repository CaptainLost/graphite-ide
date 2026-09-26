# 0524. Hold change markers in a model of their own apart from GitModel

Status: proposed
Date: 2026-09-26

## Context

`GitModel` already holds every piece of state the git panels draw — status,
branches, log, diffs — behind one debounce keyed to the repository: a file
change or a `RepositoryChangedEvent` marks it dirty, and after a quiet
period it re-runs every query the panels are currently asking for. Change
markers do not fit that shape. They are per open document rather than per
repository, and their input is not only git — the buffer the reader is
typing into is the other half of the diff — so they need to be recomputed on
every keystroke that goes quiet, independently of whatever else is
happening to the repository. Folding that into `GitModel` would mean giving
one model two unrelated notions of staleness, each ticking on its own
clock.

`ChangeBasePort` already draws the line: it is the
one question the markers need answered about git, kept apart from `GitPort`
so it can be tested without a repository. `BuildChangeMarkers` is a pure
function of two diffs and a line count. What is still missing is the thing
that drives both from a buffer on screen — reading the bases once, keeping
them until git moves, and re-diffing against the buffer as it is edited —
without either racing a slower answer onto a newer document or recomputing
a document nobody is looking at.

## Decision

`ChangeMarkersModel` (`Graphite/src/Git/ChangeMarkersModel.h`) is a model of
its own, next to `GitModel` rather than inside it, built the same way:

- A path starts being tracked on its first `Update` call, which is also
  what "track on open" comes to — the model only reads bases for a document
  it is actually asked to draw.
- Bases are read once per path and kept until they are invalidated, not
  re-read on every edit: `WorkspaceOpenedEvent` and `WorkspaceClosedEvent`
  clear every entry outright, and `WorkspaceFilesChangedEvent` or
  `RepositoryChangedEvent` mark the model dirty under the same debounce
  `GitModel` uses (0.15 s quiet, capped at 1.0 s), after which every
  tracked path's bases are re-read — hidden or not: this refresh is keyed
  to the repository, not to what is on screen, so an open tab nobody is
  looking at still gets its bases re-read alongside every other one.
- The index→buffer diff is a separate, per-document timer: an edit marks
  the entry pending and resets a 0.1 s quiet timer; once it elapses and the
  bases have arrived, the buffer is copied and handed to a worker. A
  document opened for the first time is not made to wait out this timer —
  it has not been typed into, so there is nothing to debounce. This is the
  part that a hidden document skips: with no `Update` call there is no
  edit to detect and no buffer to copy, so a tab nobody is looking at gets
  no index→buffer diff until it is shown again.
- `RepositoryGitPort` implements `ChangeBasePort`, so a bases read reuses
  the same per-thread libgit2 handle every other query on that port
  already uses (0492), rather than opening a repository handle of its own.
- Every request — for bases or for markers — takes a generation from one
  model-wide counter, mirroring `GitModel`: a slow answer that lands after
  a newer request for the same path is dropped rather than overwriting a
  fresher picture.
- `Retain` drops every path a predicate rejects, along with any answer still
  in flight for it; a path let back in later starts over with a fresh read,
  the same as one seen for the first time.
- Results are handed out as `std::shared_ptr<const ChangeMarkers>`, null
  meaning nothing to draw — untracked by git, unusable, failed, or not yet
  computed — so the gutter has one case to handle rather than three.

## Rationale

A model per kind of staleness, rather than one model tracking two clocks,
is what keeps each debounce simple to reason about on its own: `GitModel`'s
dirty flag answers "has the repository moved", and `ChangeMarkersModel`'s
per-entry quiet timer answers "has this buffer stopped changing", and
neither needs to know the other exists. Reusing `GitModel`'s generation and
delivery pattern rather than inventing a new one keeps the two models
readable side by side, and the tests borrowing the same `HeldJobs` shape
confirms the two behave alike where they are supposed to.

## Consequences

### Positive

- A document's markers survive a repository-wide refresh that touches
  every open tab, and a repository-wide refresh survives a reader typing
  in one of them — the two debounces cannot starve each other.
- Nothing is computed for a document that is not being drawn, and closing
  its tab drops its state outright rather than leaving a stale entry to
  answer to.

### Negative

- Every open document now needs its own bases read, rather than one status
  query that describes the whole repository, so opening several tabs on the
  same commit re-reads the same HEAD and index bytes once per path.
- The model duplicates `GitModel`'s debounce and delivery machinery rather
  than sharing it, since the two run on different clocks for different
  reasons; a future change to one's timing rules does not automatically
  apply to the other.

### Neutral

- An owner holding both this model and the job queue behind its
  `JobSubmitter` must declare the model before the queue, so the queue and
  its workers are torn down first — the same lifetime rule `GitModel`
  already documents on itself.
- The previous markers stay on screen until fresh ones land, even once the
  buffer has moved on from the revision they describe — the same policy
  0506 already applies to a stale diagnostic. The drawing code must
  bounds-check against the buffer's current line count rather than assume
  `ChangeMarkers::Lines` still has one entry per line.

## Alternatives Considered

### Extend GitModel with per-document state

Adding a map of per-path entries to `GitModel` directly would avoid a
second debounce loop and a second set of generation counters. Rejected:
`GitModel`'s dirty flag already means one thing, "the repository query
answers are stale", and per-document markers need a second, independent
notion of staleness driven by the buffer rather than the repository —
folding both into one dirty flag would mean every keystroke either starves
the repository refresh's own timer or forces markers onto the repository's
slower one.

### Diff on every frame instead of debouncing the buffer

Running `Core::DiffTexts` against the current buffer text on every `Update`
call, with no quiet timer, needs no per-entry state beyond the bases
themselves. Rejected: it would submit a worker job — and copy the buffer —
on every frame a document with unsaved edits is open, not only while the
reader is actively typing.

## Related

- [0523](0523-draw-change-markers-from-the-live-buffer-against-the-index-and-head.md)
  — `BuildChangeMarkers`, the pure function this model calls once both
  diffs are in hand.
- [0492](0492-give-each-worker-thread-its-own-repository-handle.md) — the
  per-thread libgit2 handle `RepositoryGitPort` reuses when it answers
  `ChangeBasePort::ReadBases`.
- [0506](0506-dim-a-diagnostic-the-buffer-has-outrun-rather-than-dropping-it.md)
  — the stale-result-stays-on-screen policy this model's markers follow too.
- `Graphite/src/Git/GitModel.h` — the debounce, generation and delivery
  pattern this model mirrors.
