# 0673. Name the repository, its remotes and HEAD in a window bar that also asks for the main line

Status: proposed
Date: 2026-11-05

## Context

The Git window opened under a folder header kept from the navigator it replaced: the folder's name and path, then
HEAD's branch with a distance chip ([0669](0669-show-references-in-a-sidebar-beside-the-history.md)). The rebuilt
window needs one place that says which repository, which remotes and which HEAD the sidebar and the history describe,
and the line model needs one place to ask the only question it cannot answer from the graph: which branch is the main
line, when no server names a default branch and the model guessed
([0664](0664-find-bases-from-server-defaults-merged-lines-and-authors-never-from-names.md)). HEAD, the remotes and the
branches are read on workers and land at different moments.

## Decision

- **One bar across the top of the window, one bar high**: the repository's folder name, its remotes ("origin · fork";
  "no remotes" only once a read said so, "…" while they are read, the read's message when it failed), and a chip for
  HEAD: the checked-out branch's name with "2 to push · 3 to pull" in words (thousands with a comma), "detached at
  <hash>", or an unborn branch's name. Clicking the chip shows HEAD's history and scrolls the sidebar once to the
  checked-out branch's row; a detached or unborn HEAD's chip answers no click. While a checkout runs the chip shows the
  loader and the branch being checked out; a HEAD that cannot be read says so in the chip's place.
- **It settles in one step** (`SettleWindowBar`): until the folder, HEAD (or its failure) and the remotes (or their
  failure) are all known, the bar holds the previous complete bar of this folder, or its waiting text when there is
  none, so it never fills in piece by piece.
- **The bar's right end says a failed read**: the repository's last failed read (status, branches, log) and the lines'
  failure, a failed line read or a line change that could not be saved, joined by " · ", cut to the room left and whole
  in its tooltip. The bar's height holds it, so it never pushes the window down. A failed line change also raises a
  toast. A commit's diff or texts that cannot be read are said beside the commit in the details, not here.
- **The main-line question** is a second bar under it while the read's main line is a guess
  (`BaseReason::MainLineGuess`): "Which branch is this repo's main line?", the guess and why, "Use <guess>" (which
  saves `UseAsMainLine`) and "Not now", which puts the question off until the folder changes. It appears in the update
  the first line read lands in.

## Rationale

The bar is the one place every view of the window shares, so the repository, its remotes and HEAD are said once, and
the question about the main line is asked where it applies to everything below it. Settling in one step and saying
failures in a fixed-height slot keep the bar from being the thing that moves when reads land.

## Consequences

### Positive

- The repository, its remotes and HEAD are named in one line whatever the history shows.
- A failed read or a change not saved is said without moving anything.
- The main line is confirmed once, with one click, only in repositories that need it.

### Negative

- The main-line question pushes the history down once, in the update the first read lands in, in repositories with no
  server default.
- "Not now" lasts until the folder changes, not across sessions.
- Two failures at once share one line, cut to the room it has.

### Neutral

- The folder's path is in the repository name's tooltip, no longer on its own line.

## Alternatives Considered

- **Keep the folder header and add the remotes to it.** Two lines of the window for what one bar says, and a header
  that filled in piece by piece as HEAD and the remotes landed.
- **Ask for the main line in the vs picker only.** The guess would stand unconfirmed until the user happened to open
  the picker, and every branch's label would rest on it meanwhile.
- **Say failed reads on a line under the window's table.** It stood outside the window's room and made the whole
  window scroll.

## Related

- [0669](0669-show-references-in-a-sidebar-beside-the-history.md): the sidebar and history under the bar.
- [0664](0664-find-bases-from-server-defaults-merged-lines-and-authors-never-from-names.md): where the main-line guess
  comes from.
- `docs/superpowers/specs/2026-11-03-git-window-lines-design.md` §4.1, §5.3 and §6.
