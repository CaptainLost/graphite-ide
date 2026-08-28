# 0508. Let the selected file win over the selected commit in the git preview pane

Status: proposed
Date: 2026-08-29

## Context

The git view is one panel split in two: the commit list above, and below it a
single pane opened by `DrawSplitResults` once something is selected. Until now
that pane had exactly one thing to draw — the diff of the file selected under
an expanded commit — so "what is selected" and "what the pane shows" were the
same question, answered by `CommitListView::m_SelectedFile`.

Showing a commit's own details (its full hash, both signatures, its parents,
its change counts and its whole message) gives the pane a second thing to draw,
and the list a second selection to hold. The two are set by different gestures
and one outlives the other, so there are frames where both are set and the pane
has to choose.

A layout constraint shaped where the details could go at all. `CommitListView`
seeks its clipper at a fixed `metrics.RowHeight`, and `CommitGraphView` places
each dot from that same height, so a commit row cannot grow taller than the
others without the graph drifting away from the rows it labels. That rules out
drawing the details inside the expanded row, which is why they went into the
shared pane.

The same constraint reaches the gesture. The chevron at the head of a commit row
is part of that row's label rather than an `ArrowButton`, precisely because a
control with its own height would advance the row past `metrics.RowHeight`. So
the chevron has no ImGui item to ask whether it was clicked.

## Decision

The chevron expands and collapses a commit row, and does nothing else. Clicking
anywhere else on the row selects the commit, and does nothing else. Because the
chevron is drawn as text, the row hit-tests it by hand: the zone runs from the
row's left edge to where the hash begins, and the press position
(`MouseClickedPos`) is what is tested, guarded on the row being hovered so a
non-mouse activation falls through to selecting.

When both a file and a commit are selected, the git preview pane draws the
file's diff. It draws commit details only when no file is selected. Selecting a
commit clears the file selection; selecting a file leaves the commit selection
standing.

Both selections survive a newly arrived page under the same rule —
`SelectionSurviving` for the file, `CommitSelectionSurviving` for the commit —
and both are dropped by `CommitListView::Reset`.

Because selecting no longer expands, `GitView::RenderCommits` asks the model for
the diff of the selected commit as well as of every expanded one, and forgets a
failed entry when a commit stops being either.

## Rationale

Separating the gestures makes the two questions independent: a reader can read a
commit's message without opening its file list, and open the file list without
losing the message. Tying both to one click — the first shape this took — meant
every attempt to re-read the message also collapsed the row the reader had just
opened.

Within the pane, picking a file is the narrower question and the more recent
one: a reader who has clicked down into a file is reading that file, and
swapping the pane back to prose about the commit would undo their last gesture.
The reverse — clicking a commit while a file from a different commit is still
selected — is the case that has to clear something, because leaving the file
selected would put one commit's diff under another commit's row.

## Consequences

### Positive

- The pane's content is a function of the two selections alone, decided in one
  place (`GitView::RenderPreview`), so no caller can leave the two disagreeing.
- Expanding and reading are independent: neither gesture undoes the other.
- Dismissing a file diff falls back to the details of the commit it belonged
  to, rather than to an empty pane.
- `CommitSummary::Body` and `Committer`, both already fetched and previously
  drawn nowhere, now reach the reader without a further query.

### Negative

- The chevron's hit zone is hand-rolled arithmetic against a measured string
  rather than a widget's own bounds, so it is not covered by any test and will
  drift if the label's leading piece is ever reordered. The zone is deliberately
  generous — it reaches the hash — which makes it wrong in the forgiving
  direction, but wrong is still possible.
- A reader cannot see a commit's message and one of its file diffs at the same
  time. Two panes would avoid that, at the cost of a third region in a panel
  that already reads top to bottom.
- Selecting a commit now costs a diff query that expanding used to be the only
  thing to ask for. It is one query per commit the reader stops on, cached and
  bounded by `GitModel::kCachedDiffs`, but a reader clicking down a long list
  asks for more than they used to.
- The change counts are dimmed until that diff lands, and again if the cache
  evicts it while the commit stays selected.

### Neutral

- Absolute timestamps are shown in the reader's own offset, resolved once per
  draw from `std::chrono::current_zone()` and falling back to UTC when the time
  zone database is unavailable. `Core::Signature` drops the author's own
  offset, so the author's local time is not recoverable and is not shown.

## Alternatives Considered

- **One click does both** — select and toggle expansion together. No new gesture
  to learn, and the diff the counts need arrives for free. Rejected because
  going back to the message means clicking the row again, which collapses it:
  the two questions fight over one gesture.
- **Click selects, double-click expands.** Keeps the gestures separate without
  any hit-testing. Rejected because a double-click in a list is invisible —
  nothing on screen says it exists, and a reader who never discovers it loses
  the file list entirely, whereas the chevron is already drawn and already reads
  as the thing that opens the row.
- **A second pane beside the diff.** Both readable at once, but it makes three
  regions in a panel whose filter boxes already run the full width above the
  split, and at ordinary widths it takes from the history column the room the
  graph and summary need.
- **Details in a detached tab, like `DiffWindows`.** Lets two commits sit side
  by side and survives the panel closing, but nothing appears without a
  deliberate second gesture, and it is the most new code for the least common
  case.

## Related

- [0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) — why the
  details pane is handed plain data and never reaches for `GitModel`.
