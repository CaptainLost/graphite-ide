# 0668. Let a tab follow a file that Graphite itself renames

Status: proposed
Date: 2026-11-03

## Context

The Explorer renames a file or folder on disk and refreshes its own tree, and
told nobody else. The watcher then saw the old path go and a new one come, so a
tab open on the renamed file was marked deleted while the same file sat in the
tree under its new name, and its unsaved edits were left on a tab with no file.
The document store already knew how to move its documents to a new path: the
open folder's own move used it, and nothing else did.

## Decision

- After a rename succeeds, the Explorer publishes `PathMovedEvent` with the old
  and the new path.
- The layer hands it to `DocumentWindows::FollowMove`, the same step the open
  folder's move takes, so the tabs under the old path move to the new one with
  their unsaved edits.
- A rename made outside Graphite is not followed: the watcher can only report a
  file gone and another come, and guessing which is which would move tabs onto
  files that merely appeared.

## Rationale

The application knows exactly what it renamed and when, so it can say so; a
rename it made itself should not look to the editor like a deletion.

## Consequences

### Positive

- Renaming a file or folder in the Explorer keeps its tabs, their edits and
  their place.

### Negative

- A tab open on a file renamed by another program is still marked deleted.

### Neutral

- The language server sees the document closed under the old path and opened
  under the new one, because its tab's path changed.

## Alternatives Considered

- **Match a gone file to a new one with the same content.** Lost: it reads every
  new file to compare, and two copies of one file would make it guess.

## Related

- [0476](0476-load-a-tab-whose-file-appears-rather-than-only-clearing-its-missing-mark.md)
  covers a tab whose file comes back at the same path.
