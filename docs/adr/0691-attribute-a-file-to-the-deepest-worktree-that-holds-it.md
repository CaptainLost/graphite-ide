# 0691. Attribute a file to the deepest worktree that holds it

Status: proposed
Date: 2026-11-09

## Context

Files of another worktree are opened in the editor from the Git window's changes view
([0689](0689-list-every-worktree-in-the-git-window-s-sidebar-ordered-by-activity-and-fixed-between-rebuilds.md)),
and also from the Explorer, search or Quick Open when the worktree sits under the open folder. Worktrees often do: in
this repository 36 of 37 lived under `D:\Repos\IDE\.claude\worktrees`, a gitignored folder inside the main checkout.
So "the file is inside the open folder" says nothing about whose file it is. A tab that reads `ToolStrip.cpp` is
ambiguous among dozens of copies, and the open folder's repository, asked about a file of a nested worktree, answers
for the wrong checkout: it calls the file ignored, or untracked, or draws it as added from top to bottom, and its blame
is of another file's history.

The open folder may also be a subfolder of its repository's working directory, and a worktree may be nested under
another worktree.

## Decision

- **Whose file it is.** A file belongs to the deepest listed worktree whose path contains it, and that wins over
  "inside the open folder". The open folder's own worktree is the repository's working directory, never the workspace
  root, so a workspace opened on a subfolder does not name its own files as another worktree's. A file that no listed
  worktree holds belongs to no worktree. The answer is cached per file for as long as the worktree list and the
  working directory stay the same, so a frame that changes neither compares no paths.
- **The tab says so.** A file of another worktree has the worktree's folder name after its own on the tab
  (`ToolStrip.cpp · eager-scribbling-frog`), drawn as the tab's dim note (`Core::DockTabLook::Note`) after the name,
  which a narrow tab cuts before the name; the tab's tooltip has the full path with the platform's separators. It is
  editable and saves into that
  worktree; the editor's handling of external changes covers an agent writing the same file.
- **The open folder's repository does not answer for it.** A file below a directory that holds a `.git` entry, file or
  folder, strictly between the working directory and the file, belongs to that nested checkout: it gets no change
  markers in the gutter, no blame and no git status colour from the open folder's repository. For the Explorer and
  the tabs this is decided once per status read, on the worker that read it (each untracked directory holding a `.git`
  entry is marked in the status), so no frame asks the disk; the gutter and blame, which ask about one file at a time,
  check its directories on their worker.
- **Opening** a changes row's file uses the row's context menu or a hover Open icon, and opens that worktree's copy
  (`worktree path / relative path`). The icon is on the open folder's Local changes rows too, since the view is shared.

## Rationale

The deepest containing worktree is the only rule that is right whatever the layout: worktrees under the main checkout,
worktrees under other worktrees, or none of them under anything. Excluding git features by the `.git` entry rather than
by the worktree list covers a nested checkout no rule ignores, and a submodule-like clone that is not a worktree at
all, with the same check. Deciding it once per status keeps the cost off the frame, where the Explorer would otherwise
walk the disk for every row it draws.

## Consequences

### Positive

- A tab from another worktree is never mistaken for the open folder's file of the same name.
- No gutter marker, blame or colour describes a file by the wrong checkout's history.

### Negative

- A file of another worktree has no git features at all here: giving it markers and blame from its own worktree is
  not done.
- Its language server is whichever one already serves its path; for a worktree under the open folder that is the open
  folder's server, whose compile database describes the main checkout.
- The status read on the worker asks the disk once per untracked directory.

### Neutral

- Reveal in the Explorer works when the worktree is under the open folder, as for any file there.

## Alternatives Considered

- **Inside the open folder means the open folder's file.** Wrong for every worktree under `.claude/worktrees`, which
  is most of them.
- **Ask the disk for a `.git` entry from the drawing code.** Correct, and a disk walk per drawn row and per frame.
- **Rely on the worktree folder being gitignored.** Works for `.claude/worktrees` in this repository and fails for a
  worktree under a folder no rule ignores, whose files the open folder's repository then reads as untracked.

## Related

- [0689](0689-list-every-worktree-in-the-git-window-s-sidebar-ordered-by-activity-and-fixed-between-rebuilds.md):
  the worktree list the attribution reads.
- `docs/superpowers/specs/2026-11-08-worktree-preview-design.md` §7 (opening a file from a worktree).
- `Graphite/src/Git/Worktrees/WorktreeOfFile.h` (`WorktreeFolderOf`, `WorktreeNameCache`, `IsInsideNestedCheckout`),
  `Graphite/src/Git/GitDecorations.h` (`IsInNestedCheckout`), `Core::FileStatus::HoldsCheckout`.
