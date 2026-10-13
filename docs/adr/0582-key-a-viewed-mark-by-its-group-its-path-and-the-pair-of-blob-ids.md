# 0582. Key a viewed mark by its group, its path and the pair of blob ids

Status: proposed
Date: 2026-10-13

## Context

A reader marks a file of a review as viewed and expects the mark to mean "I have read this file as it is now". The branch keeps moving. A commit, a rebase or an edit in the worktree can change the file after it was marked. A mark that outlives such a change tells the reader something false.

One file can be listed twice in a review, once among the commits and once among the uncommitted work ([0581](0581-show-a-branch-s-uncommitted-work-as-a-group-of-its-own-in-its-review.md)).

## Decision

A viewed mark is keyed by the branch, the group, the path as shown, the old blob id and the new blob id.

- The same pair for the same group and path: the file is viewed.
- The same group and path with another pair: the file is changed since viewed, and its row says `changed`.
- The group is part of the key, so marking a file among the commits does not mark it among the uncommitted work.

Marks of a branch missing from two branch lists read whole in a row are dropped. A rename sent from Graphite carries the marks to the new name, and a review against a renamed base follows it too.

## Rationale

**The blob ids are the content.** Two blobs that are equal are the same text, whatever commit carried them. The mark survives a rebase that leaves the file alone and drops when the file changes.

**The path alone cannot tell.** A mark by path would stay on after the file changed. A mark by commit would be lost on every harmless move of the branch.

**The group keeps two rows apart.** The same path in both groups has different blob pairs and different meanings.

**Dropping after two whole reads is safe.** A snapshot read whole lists every branch, but a read that races `git pack-refs` can miss one for that one read. A branch missing from two in a row is gone, and its marks have no place to be shown.

## Consequences

### Positive

- A mark never claims more than the reader saw.
- `changed` tells the reader which files to look at again.
- Moves of the branch that leave a file alone keep its mark.

### Negative

- A rename outside Graphite loses the marks of that branch.
- The keys are longer than a path, and each needs both blob ids from the diff.

### Neutral

- A file reverted to an earlier pair matches its old mark again if the mark was kept.

## Alternatives Considered

- **Key by path.** Simple, but a changed file stays marked as read.
- **Key by commit.** Loses every mark on each commit or rebase, including for files that did not change.
- **Clear all marks when the branch moves.** Safe, but the reader repeats work on every commit.

## Related

- [0580](0580-review-a-branch-against-its-merge-base-with-the-base-taken-from-head.md): the range whose files are marked.
- [0581](0581-show-a-branch-s-uncommitted-work-as-a-group-of-its-own-in-its-review.md): the two groups.
- [0583](0583-keep-review-marks-in-their-own-file-beside-the-window-state.md): where the marks are kept.
