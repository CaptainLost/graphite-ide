# 0504. Treat an untracked file as an added file in a working tree diff

Status: proposed
Date: 2026-08-23

## Context

The changes panel lists what the status reports, and the status is asked for
with `GIT_STATUS_OPT_INCLUDE_UNTRACKED` — so a file git has never seen appears
there as an unstaged change, ready to be staged like any other. Clicking it
opens a diff tab against the index.

That tab was empty. `QueryDiff` built its options from `GIT_DIFF_OPTIONS_INIT`
and nothing else, and libgit2's default excludes untracked files from a diff
entirely. The tab fell back to the sentence it says when a file is not in the
diff it asked for — "ConsoleSelection.h has no unstaged changes." — about a
file the panel one pane over was listing as an unstaged change.

Two smaller things stood behind the same wall. libgit2 reports such a file as
`GIT_DELTA_UNTRACKED`, which is neither of the deltas `ToChange` knew, so it
would have come through as `Unmodified`; and the delta carries an old path,
which `OnFile` copied for anything that was not `GIT_DELTA_ADDED`, sending the
whole-file view off to look for an index blob that does not exist.

## Decision

A diff whose target is `WorkTreeToIndex` or `WorkTreeToHead` is asked for with
`GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_SHOW_UNTRACKED_CONTENT |
GIT_DIFF_RECURSE_UNTRACKED_DIRS`. The other targets — both sides of which are
committed trees or the index — are unchanged.

`GIT_DELTA_UNTRACKED` maps to `FileChange::Added`, and the two path fields in
`OnFile` are decided by that mapped change rather than by the raw delta, so an
untracked file arrives with an empty `OldPath` exactly as `FileDiff` promises
for an add.

## Rationale

The status listing and the diff answer the same question about the working
tree, so they must include the same files; a row a reader can stage but not
read is a listing that lies. Naming the change `Added` rather than adding an
`Untracked` case to `FileChange` keeps the distinction where it belongs — a
status listing says "??" because a reader is deciding whether to add the file,
while a diff of it is an add and nothing else, and every view that already
draws adds draws this correctly with no change.

## Consequences

### Positive

Opening an untracked file from the changes panel shows its content, in the
hunk view and in the whole-file view alike. The whole-file view gets an empty
old side, which is what it already draws for a file added in a commit.

### Negative

`SHOW_UNTRACKED_CONTENT` reads every untracked file the pathspec admits, and
`RECURSE_UNTRACKED_DIRS` walks into directories the default would have
collapsed to a single entry. For an unfiltered working-tree diff in a tree with
a large untracked build output, that is real work git would otherwise have
skipped.

### Neutral

An ignored file stays out: `GIT_DIFF_INCLUDE_IGNORED` is not set, and the
status listing does not offer those rows either.

## Related

- [0502](0502-read-a-working-tree-file-s-text-from-the-index-and-the-disk.md)
