# 0526. Highlight what a changed line lost by its shared prefix and suffix

Status: proposed
Date: 2026-09-27

## Context

Resting the pointer on a change marker in the gutter (0525) opens a tooltip
that shows the lines the change replaced: from the index for an unstaged
change and from HEAD for a staged one (0523). The new lines are already on
screen beside the marker, so the tooltip shows only the old ones.

A modified line often differs from its old self by a few characters: a
renamed identifier, a changed literal, one punctuation mark swapped for
another. Shown whole, the old line has to be compared against the new one by
eye, character by character, to find what changed. Marking the part that is
gone answers that at a glance, but it needs a diff within the line, and the
code base had none: the diff view colours whole rows.

The tooltip is drawn every frame while the pointer rests on a marker, for at
most `kShownOldLines` (15) old lines, so whatever computes the mark runs on
the UI thread and must be cheap.

## Decision

- Pair each old line of a modified hunk with the buffer line in the same
  position of the hunk. `RemovedSpan` (`Git/LineSpans.h`) marks everything in
  the old line between the bytes the two share at the start and the bytes
  they share at the end, with the shared suffix never allowed to reach into
  the shared prefix.
- Move both ends of the span onto UTF-8 character boundaries, so a character
  whose lead byte matches its replacement's is never cut in half.
- Highlight an old line with no partner in the new lines whole: it is gone
  entirely. Do not highlight the lines of a deletion. They are all gone, and
  the heading already says so.
- Keep the old lines in `ChangeHunk::OldLines`. The worker that builds the
  markers fills them, since it already holds both base texts. Only the prefix
  and suffix are worked out when the tooltip is drawn.

## Rationale

One edit per line is the common case, and for it the shared prefix and
suffix are exact. The computation is two linear scans with no tuning and no
allocation, and it gives the same answer every frame. A character diff would
be more precise for a line with several separate edits. It would also cost
far more, and it needs choices (granularity, cleanup of short matches) whose
answers show up as flicker in a tooltip that is redrawn continuously.

## Consequences

### Positive

- The reader sees what a changed line lost without comparing it to the new
  line themselves.
- No new dependency and no work off the UI thread beyond what the markers
  already do.

### Negative

- Two edits far apart in one line highlight everything between them,
  including text that did not change. `LineSpansTests` pins this behaviour.
- Pairing by position is wrong when a hunk inserts lines before the one it
  edits: the old line is then compared with an unrelated new one and comes
  out highlighted almost whole.
- A staged hunk that unstaged edits have split or partly rewritten is not
  paired at all (`ChangeHunk::Intact`). Its new lines no longer sit where
  the hunk put them, so its old lines are highlighted whole.

### Neutral

- Tabs are expanded separately in each piece of the line. A tab after the
  highlighted part may land a column away from where the file puts it. The
  tooltip is read, not aligned against anything.

## Alternatives Considered

- **A character diff (LCS) per paired line.** More precise for several edits
  in one line. Lost on cost and stability, as above. Kept as the next step if
  the imprecision proves to matter.
- **No highlight, old lines only.** The simplest option, and it leaves the
  character-by-character comparison to the reader, which is the work the
  highlight exists to remove.
- **A word diff.** Words need a definition that differs by language and
  breaks on punctuation-heavy code. It also gives a noisier answer than the
  prefix and suffix for the common single edit.

## Related

- 0523 Draw change markers from the live buffer against the index and HEAD
- 0524 Hold change markers in a model of their own apart from GitModel
- 0525 Reserve a change column in the gutter whether or not git is present
