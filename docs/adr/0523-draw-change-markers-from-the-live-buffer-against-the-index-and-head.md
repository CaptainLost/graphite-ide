# 0523. Draw change markers from the live buffer against the index and HEAD

Status: proposed
Date: 2026-09-26

## Context

The gutter is to show, per buffer line, how that line differs from git —
and it needs to show two different things at once: what the reader has
typed but not staged, and what is staged but not yet committed. Those are
two separate comparisons — buffer against index, and index against HEAD —
and a buffer line does not sit at the same offset in the index as it does
on disk once either comparison has inserted or removed lines above it.
`Core::LineHunk` (from `Core::DiffTexts`) gives a diff as a list of runs, not
a per-line answer, so something has to turn two such lists into one marker
per buffer line before the gutter can draw anything.

## Decision

`BuildChangeMarkers` (`Graphite/src/Git/ChangeMarkers.h`) takes both diffs —
`headToIndex` and `indexToBuffer` — plus the buffer's line count, and returns
one `LineChange` per buffer line plus a sorted list of `DeletionAnchor`s for
lines that no longer exist.

- `indexToBuffer` is the unstaged change, expressed in buffer lines already,
  and is applied first, in full.
- `headToIndex` is the staged change, expressed in index lines. Each of its
  lines and boundaries is carried into buffer coordinates by walking
  `indexToBuffer` and accumulating the length each of its hunks adds or
  removes before the point in question.
- Where an unstaged hunk covers the same line or boundary a staged one maps
  to, the staged marker is dropped rather than drawn underneath: the line
  in front of the reader no longer matches what git would show as staged
  there, so showing it as staged would be a marker for a change no longer
  present in the buffer.
- A hunk's kind is decided per hunk: no old lines means `Added`, lines on
  both sides means `Modified`, no new lines produces a `DeletionAnchor`
  instead of a `LineChange`.
- Two deletion anchors resolving to the same buffer boundary keep only the
  unstaged one, for the same reason a staged line marker loses to an
  unstaged one on the same line.

## Rationale

Comparing the buffer to the index and the index to HEAD separately, rather
than trying to build one combined diff against HEAD, is what lets the two
be told apart on screen — full colour for the change still being made,
45% alpha for the change already staged — while keeping `Core::DiffTexts`
ignorant of git entirely. Carrying the staged diff through the unstaged one
by re-mapping its coordinates, instead of running a third diff of HEAD
against the buffer, keeps the two comparisons independent and avoids a
third pass over the text for every buffer redraw.

## Consequences

### Positive

- The gutter can draw an unstaged and a staged change on the same buffer
  independently, each in its own shade, without a combined three-way diff.
- `BuildChangeMarkers` is pure and depends on nothing but `Core::LineHunk`,
  so it is tested with hand-built hunks and no repository, editor, or file
  on disk.

### Negative

- Mapping a staged line through the unstaged hunks is a linear scan per
  staged line rather than a single pass over both diffs merged; for the
  hunk counts a single file produces this is not a measured concern, but it
  is a cost a future change touching this function should keep in mind.
- A staged change sitting under an unstaged one is dropped rather than
  merged or queued, so the reader only ever sees one marker per line — a
  simplification that would need revisiting if two markers on one line were
  ever wanted.

### Neutral

- An untracked file is added throughout, consistent with
  [0504](0504-treat-an-untracked-file-as-an-added-file-in-a-working-tree-diff.md):
  `headToIndex` is empty and `indexToBuffer` covers every line as `Added`.
- A file inside a submodule of the open repository — some directory above
  it is a gitlink (mode 160000) in the index — gets no markers at all, the
  same as an ignored file or one outside the repository. It belongs to the
  submodule's own repository; read against this one it would have neither
  side and so be drawn as added throughout. `Core::Repository::IsInsideSubmodule`
  answers the question, and `RepositoryGitPort::ReadBases` returns
  untracked for such a path before reading either side.
- A newly staged file is staged-added throughout in the same way, with
  `headToIndex` covering every line and no unstaged hunk to map it through.
- Diffing the buffer against a branch other than the current HEAD is not
  covered; `headToIndex` is always HEAD-to-index.

## Alternatives Considered

### Diff HEAD against the buffer directly, in one pass

A single `Core::DiffTexts(head, buffer)` would need no coordinate mapping at
all. Rejected because it collapses staged and unstaged into one comparison,
losing the distinction the gutter needs to draw: it cannot tell "the reader
changed this line" from "this line was already staged" once both are folded
into one HEAD-to-buffer hunk.

### Merge both hunk lists into one pass instead of mapping staged lines through the unstaged diff

Walking `headToIndex` and `indexToBuffer` together in a single merge, the way
a three-way diff library would, avoids the repeated re-mapping this
decision does per staged line. Rejected for this task: the merge has to
reproduce the same "unstaged wins" rule at every point the two lists
disagree, which is no simpler to write correctly than mapping coordinates,
and the mapping keeps each diff's own shape legible in isolation.

## Related

- [0504](0504-treat-an-untracked-file-as-an-added-file-in-a-working-tree-diff.md)
  — the untracked-file precedent this record's markers stay consistent with.
- `Core/src/Core/Git/TextDiff.h` — `Core::LineHunk` and `Core::DiffTexts`,
  the diff this function consumes.
