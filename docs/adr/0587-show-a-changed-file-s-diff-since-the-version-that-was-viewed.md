# 0587. Show a changed file's diff since the version that was viewed

Status: proposed
Date: 2026-10-14

## Context

A viewed mark records the pair of blobs the reader saw ([0582](0582-key-a-viewed-mark-by-its-group-its-path-and-the-pair-of-blob-ids.md)). When the branch gains a commit that touches the file, the mark no longer matches and the file is shown as changed. The review's diff is still the whole change against the base, so the reader must find the new part in it by eye.

The question at that moment is "what is different from what I read?".

## Decision

For a file whose viewed mark no longer matches, the diff column offers "All changes | Since you viewed". The default is "Since you viewed"; the choice is a setting of the session ([0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md)).

- "Since you viewed" diffs the blob the mark recorded against the file's current version: the tip's blob for a committed file, the file on disk for an uncommitted one.
- It is unavailable, and says so, when the viewed blob is no longer in the object database. "All changes" is shown instead.
- When only the base moved, the file's own version is the one that was viewed. There is nothing new, and the column says so.
- "Hide white space" applies to every diff the review shows, including this one. A file changed only in white space stays listed, and its diff says "Only whitespace changed.".

## Rationale

**The recorded blob is the version that was read.** Diffing from it to the current version is exactly the change since, whatever happened to the base in between.

**Default to the new part.** A file whose mark went stale is a file the reader has already read; the new part is what is wanted first, and the whole change is one click away.

**Say why, not nothing.** A blank column for a vanished blob or an unmoved file reads as a fault. A sentence tells the reader which case it is.

**White space is the reader's view of a diff, not a property of one kind of it.** One switch that covers every diff is easier to predict than a switch per view. Keeping a white-space-only file in the list keeps the list the same as the set of changed files.

## Consequences

### Positive

- After a new commit the reader sees only what changed in the files they had read.
- Marks are unaffected by white space, since they are keyed by blob ids.

### Negative

- A viewed version that was never committed or staged can be pruned, and then there is no diff since.
- The diff column has one more switch, shown only for changed files.

### Neutral

- Line counts in the list and the summary follow the white-space setting.

## Alternatives Considered

- **Always show the whole change and mark the new hunks.** Needs the viewed version too, and hunks of the whole change do not line up with the changes since.
- **Keep the viewed text in the marks file.** Always available, but stores whole files in a file meant to hold small marks.
- **Hide white space only for the all-changes diff.** A switch that applies to one of two views is a surprise on the other.

## Related

- [0582](0582-key-a-viewed-mark-by-its-group-its-path-and-the-pair-of-blob-ids.md): the pair of blobs a mark records.
- [0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md): the session whose setting this is.
- [0583](0583-keep-review-marks-in-their-own-file-beside-the-window-state.md): where the setting is kept.
