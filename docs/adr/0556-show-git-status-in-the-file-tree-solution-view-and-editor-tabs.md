# 0556. Show git status in the file tree, solution view and editor tabs

Status: proposed
Date: 2026-10-04

## Context

A reader looks for files in three places: the Explorer's file tree, the
solution view and the editor's tabs. None of them said anything about git.
The only place a changed file showed up was the Changes panel, so seeing which
files in a project had been touched meant leaving the tree for another panel
and matching names by eye. A QA report raised it as a missing feature.

The gutter already speaks this language per line: [0525](0525-reserve-a-change-column-in-the-gutter-whether-or-not-git-is-present.md)
draws added, modified and removed markers in the scheme's `GutterAdded`,
`GutterModified` and `GutterRemoved` colours. The trees had a second,
unrelated use of colour: a file open in the editor, but not the active one,
was drawn in the scheme's accent name colour. That choice was made as a QA fix
and no record carries it. A status colour on the name would collide with it.

`GitModel` hands out an immutable `Core::WorkingTreeStatus` snapshot for every
status it applies, with paths relative to the repository's working directory.
The trees and tabs hold absolute paths and ask about every visible row on
every frame.

## Decision

- Give every file one state: what is on disk against HEAD. Staged and unstaged
  are not told apart in the trees; the Changes panel keeps that distinction.
- Rank the states, highest winning when a file carries several:

  | State | Letter | Colour slot | From `Core::FileStatus` |
  |---|---|---|---|
  | Conflicted | `!` | `GutterRemoved` | `IsConflicted` |
  | Modified | `M` | `GutterModified` | Index or WorkTree `Modified` or `TypeChanged` |
  | Renamed | `R` | `GutterModified` | Index or WorkTree `Renamed` |
  | Added | `A` | `GutterAdded` | Index `New` |
  | Untracked | `U` | `GutterAdded` | `IsUntracked` |

  `Graphite::GitDecoration` is that enumeration, ordered by priority.
  Add no new scheme keys: the three gutter colours are reused, read through
  `ReadGitPalette`.
- A deleted file has no row, so it carries no letter, but it weighs on its
  folders as Modified, so a folder whose only change is a deletion still shows
  a dot.
- Treat an untracked directory as one entry, as git reports it. The folder
  itself shows `U`; every path beneath it is Untracked; its ancestors
  aggregate it like any other change (a dot, not a letter).
- A file moved out of a folder weighs on the folder it left as a deletion
  would, so the source folder of a rename gets a dot as well as the target.
- Leave ignored files undecorated; they keep their dimmed look. The exception
  is a tracked file under an ignore pattern (force-added, say): git reports it
  as ignored too, but it has its own status entry, so the file tree shows that
  entry like any other. Only what a row would inherit from an untracked
  directory around it is withheld, so ignored files inside an untracked
  directory stay plain.
- A deleted or missing file in a project does not weigh on the project's dot in
  the solution view; there is no row to point at.
- In the file tree and the solution view, draw the name in its state's colour
  and the letter right-aligned at the visible right edge of the row. Draw a
  filled dot in the same column on a folder, a project or a solution folder
  that has any decorated descendant, in the colour of the highest state among
  them, whether expanded or collapsed. The folder's own name keeps its normal
  colour.
- Mark every file that is open in the editor, the active one included, by a
  dot drawn in the gap where a sibling folder draws its arrow, in the
  checkmark colour, in place of the accent name colour (a ring for the preview
  tab, below). The active file also keeps its selection, so the mark always
  means "open in a tab" and does not vanish from the file the reader is
  working in. The name colour is thereby free to carry the status, and an open
  file with a status shows both.
- Mark a file open in the preview tab by a ring of the dot's size and colour
  instead of the filled dot, so the tree tells apart the one file the next
  preview will replace. The ring turns into the dot when the tab is made
  permanent. A shape and not the tab's dimming: a dot a few pixels across at
  0.6 of its alpha does not read apart from a full one on a dark background.
- Colour a tab's label in the file's state colour and draw no letter. A
  preview tab uses the same colour at 0.6 of its alpha, so it still reads as a
  preview next to a permanent tab; an undecorated preview keeps the disabled
  colour, and an untitled document has no state.
- Build the lookup, `GitDecorations`, once per status snapshot: the layer
  compares the snapshot pointer and rebuilds only when it changes. A folder's
  state in the file tree is the maximum over its descendants, computed while
  building, so per-frame lookups are hash probes and a clean tree builds no
  key. The solution view's project and solution-folder aggregates are not in
  the index: the view computes them lazily from the files it lists and caches
  them per index version.
- Key the lookup by path relative to the repository's working directory
  (`GitModel::GetWorkingDirectory`), through `Core::PathKey`, so lookups are
  case-folded on Windows and indifferent to the separator. The layer hands a
  `GitDecorationView` to the Explorer, the solution view and the tabs; with no
  folder, no repository or no status yet, the view is empty and nothing
  anywhere changes.

## Rationale

Status belongs where files are found, and the one-state model is what a tree
can show without becoming a second Changes panel. Reusing the gutter colours
keeps one meaning for each colour across the application and needs no scheme
migration. Folder dots answer "is anything changed in here" without expanding
it. Moving the open-file marker to a dot frees the name colour for the status
rather than overloading one channel with two meanings.

## Consequences

### Positive

- A changed file is visible in the tree, the solution view and its tab without
  opening the Changes panel.
- A collapsed folder still reveals that something inside changed.
- Per-frame cost is independent of the number of changes.

### Negative

- Staged and unstaged changes look the same in the trees, so a file that is
  staged and then edited again shows once, as Modified.
- The accent name colour no longer marks an open file; anyone used to it must
  look for the dot instead.
- A scheme that gives the three gutter colours poor contrast against the tree
  background makes names harder to read; there is no separate setting.

### Neutral

- Files outside the repository, and the results of Find in Files, get nothing.

## Alternatives Considered

- Letters only, no name colour: lost because colour is what the eye finds
  first in a long list, and the letter alone is easy to miss.
- Separate staged and unstaged marks in the trees: lost because it crowds a
  row that has room for one letter, and the Changes panel already answers it.
- New scheme keys for tree status: lost because the gutter colours already
  mean added, modified and conflicted, and a second set would drift.
- Keep the accent colour for open files and show status by letter only: lost
  because it left folders and tabs with no way to carry status at a glance.
- Query git per row: lost because it would put a repository call on the
  render path for every visible row.

## Related

- [0525](0525-reserve-a-change-column-in-the-gutter-whether-or-not-git-is-present.md): the gutter colours reused here.
- [0523](0523-draw-change-markers-from-the-live-buffer-against-the-index-and-head.md): the per-line counterpart to this per-file view.
- [0490](0490-colour-the-application-chrome-from-the-active-scheme.md): the scheme the colours come from.
- The accent-coloured open-file marker this replaces was never recorded in a
  record, so there is nothing to supersede formally.
