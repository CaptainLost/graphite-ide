# 0630. Leave the branch out of the title bar, since the status bar shows it

Status: proposed
Date: 2026-10-21

## Context

[0619](0619-show-the-open-folder-and-branch-in-the-title-bar.md) put the open
folder and its branch in the title bar, after the menus, so that a window could
be told from another without a panel shown. The branch there is display only:
no click, no tooltip, worded by the status bar's own function so the two never
disagree.

The status bar shows the same branch a few hundred pixels below
([0535](0535-show-git-state-in-the-status-bar-read-only-and-keep-git-actions-in-the-panels.md),
[0620](0620-show-problems-and-the-server-s-health-as-glyphs-and-colours-in-the-status-bar.md)),
and shows more of it: a click reveals the Git panel, a tooltip names what it
tracks, and the distance and the change count stand beside it. Used for a day,
the copy at the top read as noise: the reader said it "makes no sense, because
we have it at the bottom too".

## Decision

Draw only the divider and the open folder after the menus; draw no branch in the
title bar.

- `TitleContext` holds the folder alone; `GraphiteLayer` no longer reads the
  branch for it.
- `Core::LayOutCaption` places the divider and the folder only; the gap that
  stood between the folder and the branch is gone. The folder is still never
  shortened, and is dropped with its divider before it would reach the window
  buttons.
- `GitBranchLabel`, which existed only to word the title bar's branch, is
  removed. The status bar keeps the branch as it was.

The rest of 0619 stands: the folder, the menus' sizes, the window buttons and
the logo.

## Rationale

One place for a fact keeps the chrome quiet, and of the two places the status
bar is the one that does something with the branch. The folder stays because
nothing else on screen names it when the Explorer is closed.

## Consequences

### Positive

- The title bar carries one label instead of two, and a long folder name no
  longer competes with a branch for the room before the buttons.
- The branch has one rendering, the clickable one.

### Negative

- A window whose status bar is hidden behind another window's edge, or a
  screenshot cropped to the top, no longer says which branch it is on.

### Neutral

- The status bar, the Git panel and the branch's wording are unchanged.

## Alternatives Considered

- **Keep the branch at the top and drop it from the status bar.** The top one
  takes no click, and making it clickable would put a button in the drag region,
  which 0619 already turned down.
- **Keep both.** It is what the reader found redundant.

## Related

- [0619](0619-show-the-open-folder-and-branch-in-the-title-bar.md): the title bar
  this record takes the branch out of.
- [0620](0620-show-problems-and-the-server-s-health-as-glyphs-and-colours-in-the-status-bar.md):
  the status bar that keeps it.
