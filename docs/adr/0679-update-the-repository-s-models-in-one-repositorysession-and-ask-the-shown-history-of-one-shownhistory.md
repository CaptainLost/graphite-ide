# 0679. Update the repository's models in one RepositorySession and ask the shown history of one ShownHistory

Status: proposed
Date: 2026-11-08

## Context

The Git window's models were brought up to date in two places. The layer ran
`GitModel`, the write consequences, `NetworkModel`, `LinesModel` and
`WorktreesModel` in an order fixed by warning comments, and fed the line read's
base tips back into the authors; `GitView::OnUpdate` then made `ContentModel`
follow the read. The frame tests copied the layer's order by hand, so a change
to it had to be made twice and the copy could drift.

Inside the window, which history is shown — the reference it names, the line it
is compared with, whether it waits for a read — was answered by a dozen
`GitView` methods. They read the read through the sidebar's row snapshot, which
was rebuilt only while the sidebar was drawn, so the history's answers depended
on the sidebar being drawn first in the frame. The pure helpers they feed were
tested; how they were fed could be reached only through ImGui frames.

## Decision

- `Git/RepositorySession` updates the repository's models every frame, before
  any panel, in one order: the git model, what a landed write changes, the
  network model, the lines and worktrees following the branch listing, the base
  tips the authors leave out, the content checks following the read. The layer
  and the frame tests call `Update` and nothing else; the layer then takes the
  network results and follows the read into the reviews.
- `Git/Lines/LinesOnScreen` holds the read the Git window draws with its
  `BranchLines` and checked-out ref. `GitView` follows it as each frame starts,
  so the window bar, the sidebar and the history read one read whichever is
  drawn first. The sidebar keeps its own snapshot only for building its rows.
- `Git/History/ShownHistory` answers which history is shown from that read, the
  selection, HEAD and the models' reading state, made per question by
  `GitView::Shown()`. The stateful log asking — what was asked, what is held,
  which rows are on screen — stays in `GitView`.

## Rationale

The order is one concern and now has one owner the tests drive as the app does.
The shown history's answers are a function of a handful of inputs, so they are
tested as such, and the read they come from no longer depends on draw order.

## Consequences

### Positive

- The frame tests run the layer's order instead of a copy of it.
- The content checks follow the read whether or not `GitView` updates, and in
  the same frame as the lines.
- `ShownHistory` has tests for HEAD after a checkout, a branch's fold and
  distance, a stash, a tag and a restore that waits.

### Negative

- `ContentModel` now learns that the window is on screen one frame after it
  follows a new read, as `WorktreesModel` already did.
- `ShownHistoryInput` holds references; a `ShownHistory` must not outlive the
  call that made it.

### Neutral

- The checkout flow — planned, asked, held for the panel — is still the
  window's, and the layer still asks `GitView::CurrentWait` for it.

## Alternatives Considered

- **Let the session own the models.** Deferred: the layer's member order joins
  the workers before the models and the port go away, and moving ownership
  would move that order too, for no change in behaviour.
- **Move the log asking into `ShownHistory` as well.** Deferred: it asks the
  git and lines models and remembers what it asked, which would make the module
  a second view of `GitView`'s state rather than a function of its inputs.

## Related

- [0667](0667-apply-what-a-landed-git-write-changes-in-one-module-outside-the-panels.md)
  is the write consequences this session runs right after the git model.
- [0663](0663-compare-each-branch-with-the-line-it-forked-from.md) is the line
  the shown history compares a branch with.
