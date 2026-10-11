# 0575. Filter the file tree against a listing of the whole folder read in the background

Status: proposed
Date: 2026-10-11

## Context

The Explorer's file tree has a filter box. It matched only the nodes the tree
had already read, and `Core::FileTree` reads a folder the first time it is
opened. A file in a folder nobody had opened could not be found: the folder
had no children yet, so nothing under it matched and the filter hid the
folder.

Reading the whole tree when the user types into the filter is too slow for the UI
thread. The repository's own checkout holds about 23,000 files in 4,100
folders once `bin`, `bin-int`, `.vs` and the worktrees are counted. Asking git
whether each of them is ignored costs more than reading them.

## Decision

- Match the filter against `FolderListing`, a list of the whole open folder
  that `ListFolder` reads on a worker. The worker opens its own repository
  handle ([0492](0492-give-each-worker-thread-its-own-repository-handle.md)).
- List a folder git ignores, and `.git`, but never enter them: their own names
  can match, what they hold cannot. List an ignored file and mark it, so
  "Hide" in the ignored-files setting hides it as the tree does.
- Read the listing only while the filter has text. Mark it out of date when the
  workspace reports a change outside every folder it did not enter, on a full
  refresh, and when another folder opens. Run one read at a time, and keep an
  out-of-date listing on screen until the next read replaces it.
- While filtering, draw a separate tree, `BuildFilteredTree`, made of the
  matches and the folders leading to them, under an ImGui ID scope of its own.
  Leave the real `FileTree` untouched, so clearing the filter leaves every
  folder as it was before. Open every folder holding a match when the filter
  text changes; when only the listing was read again, keep what the user
  collapsed.
- Reveal the active document in the tree only while the filter is empty, so a
  file opened from the results is revealed once the filter is cleared.
- Let rename, delete, duplicate and new file in the results act through the
  real tree as before; the results follow on the next read.
- Filter the solution view the same way from the solution's own model, which
  holds every project's files once the solution is read: leave out a row that
  neither passes by its own name nor holds a row that does, open every row
  holding a match when the filter text changes, hide the references while
  filtering, and draw the rows under an ImGui ID scope of their own.

## Rationale

A worker read keeps typing responsive whatever the folder's size. Skipping
ignored folders keeps the read proportional to the files a user works on, the
same trade the project search makes with "Respect .gitignore". Drawing the
results as their own tree avoids asking git about every loaded node on the UI
thread, and leaves no open state to restore when the filter is cleared.

## Consequences

### Positive

- Any file outside an ignored folder can be found by name, whether its folder
  was ever opened or not.
- Clearing the filter returns the tree exactly as the user left it.
- The solution view finds a file in a project nobody opened, as the file tree
  does.

### Negative

- After the first keystroke in a large folder, the results wait for the read
  to end, with a loader in their place once the wait passes 200 ms
  ([0576](0576-draw-a-loading-indicator-through-a-core-facade-over-imspinner.md)).
- Build output and other ignored content cannot be found through the filter.
- A file created or duplicated from the results shows up there only after the
  next read, so its inline rename may not start.

### Neutral

- A row that passes by its own name but holds no match, in either view, is
  shown closed and opens empty: the filter narrows what is inside it too.

## Alternatives Considered

- **Read the whole tree into `Core::FileTree` when the filter gets text.** On
  the UI thread it stalls typing on a large folder; on a worker it would need
  the tree, which every panel reads each frame, to be handed between threads.
- **Load only the folders leading to matches into `Core::FileTree`.** A broad
  filter loads most of the tree anyway, asking git about every node on the UI
  thread. Rows opened for the filter would also have to be closed again by
  hand once it is cleared, since ImGui keeps their open state under the same
  IDs.
- **Show the results as a flat list of paths.** Simpler to draw, but rejected
  in favour of keeping the tree's shape, so a match is seen where it lives.

## Related

- [0492](0492-give-each-worker-thread-its-own-repository-handle.md) — the
  worker's own repository handle.
- [0556](0556-show-git-status-in-the-file-tree-solution-view-and-editor-tabs.md) —
  how ignored rows look in the tree.
