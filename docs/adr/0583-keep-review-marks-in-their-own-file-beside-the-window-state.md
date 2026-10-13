# 0583. Keep review marks in their own file beside the window state

Status: proposed
Date: 2026-10-13

## Context

Per-folder state lives in `<folder>/.graphite/` ([0522](0522-persist-session-state-in-two-scopes-global-window-and-panels-per-workspace-tabs.md)). `workspace.json` there is rewritten from a snapshot of the window layout taken every frame. It describes what is open.

Viewed marks ([0582](0582-key-a-viewed-mark-by-its-group-its-path-and-the-pair-of-blob-ids.md)) are not layout. They are the reader's work, they change on a click, and they must survive a closed tab. Line comments, planned for a later part of the review, need the same kind of home.

## Decision

Review marks live in `<folder>/.graphite/reviews.json`, a versioned JSON file.

- It is written atomically, two seconds after the last change, and at once when the folder closes or another folder opens, and when the application shuts down.
- A file whose marks could not be taken in is never simply written over. One that does not parse, or is of a version this build does not know, is first copied to `reviews.json.bak`, and is not written over if the copy fails. One that cannot be read at all is not written until the folder is opened again.
- `workspace.json` keeps only the open review tabs, under `restore.review.*`.
- Line comments will be kept in `reviews.json` too.

## Rationale

**Different lifetimes.** A tab is open or closed. A mark outlives the tab, so the two cannot share a file rewritten from the window's layout.

**Two seconds batch the clicks.** Marking ten files in a row is one write. Writing at once when the folder closes or another opens, and when the application shuts down, means the last clicks are not lost.

**A version field allows change.** The file will gain comments, and the reader of an older file must be able to tell what it holds.

**What was not read is not written over.** A file that opened with no marks because it could not be taken in still holds the reader's earlier work. Writing the first new mark over it would lose all of that work.

## Consequences

### Positive

- Closing a tab does not lose its marks.
- The per-frame snapshot of the window never touches the marks.
- Comments have a home when they come.
- A hand edit that breaks the file, or a file written by a newer build, is kept as `reviews.json.bak`.

### Negative

- A crash within two seconds of the last click loses that click.
- Marks made while the file cannot be read are lost when the folder closes.
- There is one more file in `.graphite/` to keep in step with the folder.

### Neutral

- Marks follow the folder, not the user, like the rest of the workspace state, and are not kept in the profile ([0531](0531-keep-per-user-state-in-the-user-s-profile-not-beside-the-executable.md)).

## Alternatives Considered

- **Put the marks in `workspace.json`.** One file less, but a file rewritten from a per-frame snapshot of the layout then also holds the reader's work.
- **Write on every click.** Loses nothing, but writes the file ten times for ten clicks.
- **Keep the marks in the profile.** They would follow the user rather than the folder, and would not match the branches of any other clone.

## Related

- [0522](0522-persist-session-state-in-two-scopes-global-window-and-panels-per-workspace-tabs.md): the two scopes this follows.
- [0582](0582-key-a-viewed-mark-by-its-group-its-path-and-the-pair-of-blob-ids.md): what a mark is.
- [0531](0531-keep-per-user-state-in-the-user-s-profile-not-beside-the-executable.md): why this is not in the profile.
