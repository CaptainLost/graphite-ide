# 0669. Show references in a sidebar beside the history

Status: proposed
Date: 2026-11-04

## Context

The Git window opened on a navigator of work cards, threads, the six newest local branches and the stashes beside one
main area of five views ([0658](0658-open-the-git-window-on-a-navigator-of-work-changes-branches-and-stashes-beside-one-main-area.md)),
with every branch in a table in that area ([0657](0657-browse-every-branch-in-a-table-in-the-git-window-s-main-area.md))
and a card for each branch that held work against one trunk
([0654](0654-count-a-branch-as-holding-work-while-a-commit-of-its-own-has-no-patch-twin-in-the-trunk-or-its-worktree-has-changes.md)).
In a repository with thousands of branches, hundreds of people and several remotes, six branches hid the rest behind a
second view, and the cards counted against a trunk that each branch's own line now replaces
([0663](0663-compare-each-branch-with-the-line-it-forked-from.md),
[0664](0664-find-bases-from-server-defaults-merged-lines-and-authors-never-from-names.md)).

The line model that describes each branch is read on a worker for each branch snapshot. It lands after the branch list,
on a first opening without a cache seconds after it, and a list that fills in as it lands would put rows on screen out
of nowhere.

## Decision

Draw the Git window as a sidebar of every reference and stash beside the history of the one selected.

- **Frame.** Under the window bar ([0673](0673-name-the-repository-its-remotes-and-head-in-a-window-bar-that-also-asks-for-the-main-line.md)),
  a table of two columns named `gitWindowA`: the sidebar, 300/13 em wide to start and resizable, and the history with
  its split preview.
- **Sections**, built by `BuildSidebarRows`, in this order and each collapsible:
  - **Yours**: every local branch, the checked-out one first, then the newest first. A base's local copy is in the
    base's row, not here.
  - **Bases**: the active bases, then a group "Quiet for 90 days", then "Why these, and how to change them".
  - **One section per remote**, "not on this computer": its branches except the bases and those that are one row with
    a local branch, merged ones behind "N merged hidden · Show", grouped by activity past 15.
  - **Stashes**.

  A search field above them matches every word against a branch's name and its authors.
- **Rows** are rebuilt only when what they are built from changes: the line read, the stash list, the authors (held as
  they were when a search began, for as long as it runs), the search text, a section, group or merged-branches choice,
  or the shown branch. Remote branches are grouped by activity against the read's time, never the clock's, so a row
  moves to an older group only with a new read. Only the rows in view are described and drawn. A branch row shows its
  history, a stash row the stash's; a base row its local copy's when the copy is checked out or has commits of its
  own, else the base's, and its menu is the local copy's whenever there is one, its lines items acting on the base. A
  section or group row opens or closes; "in Yours" opens Yours and scrolls to it. A right-click opens the ref menu
  ([0541](0541-put-branch-and-stash-actions-in-the-git-panel-s-right-click-menus.md)) or the stash menu. A menu whose
  row comes to stand for another reference when the rows are rebuilt closes rather than act on that one.
- **The shown branch stays listed**: its row is listed even when merged, and the group or quiet bases holding it open
  unless the user closed them, so clearing a search or a fetch never hides the branch on screen.
- **Waiting.** Until the first read of the lines lands, one waiting row stands in for the sections that need it:
  "Reading 3,448 branches…" once the branches are listed, "Reading branches…" before. The Stashes section is listed below
  it at once. A failed read or branch listing says so in that row ("Cannot read the branches' lines: …") with no loader
  beside it, since nothing is on its way. Once a read is in hand, a later failure leaves its rows on screen. The user's
  e-mail, which a row needs to say "you", is read beside every branch listing, so it never waits for the authors walk.
- **Saved state** is the shown reference alone: `restore.gitWindow.ref`, a full name, empty for HEAD's history. The
  old `restore.gitWindow.view` and `restore.gitWindow.branch` are neither read nor written, so a folder saved with them
  opens on HEAD's history; nothing is migrated. A restored reference waits for the branches, and one that is gone
  shows HEAD's history (`RefToRestore`). A selected stash is not kept.
- **Removed:** the Work cards and their facts, the threads and their header, the navigator, the branch table, the
  review embedded in the window and the review progress the cards and threads showed. The branch menu's "Review" and
  "Open review" open the review tab, keeping a draft as a session first.
- The window's Changes view is the preview of the history's Local changes row, the first row of HEAD's history and of
  the checked-out branch's ([0656](0656-draw-changes-as-one-view-hosted-by-the-side-panel-and-the-git-window-sharing-one-commit-draft.md)).

## Rationale

One sidebar keeps every branch a click away from its history, where the navigator kept six and a second view the rest;
sections by where a branch lives say whether it is on this computer without a column to read. One waiting row replaced
once is the rule every list follows, and keeping only the reference makes a restore fall back to the one view that
always exists.

## Consequences

### Positive

- Every local branch, remote branch, base and stash is in one list whose cost is the rows in view.
- The window lists no worktrees and matches no patches; the lines are read when the folder opens, whether or not the
  window is on screen.
- The stashes and HEAD's history can be used while the lines are read.

### Negative

- Until the first read lands, no branch can be picked in the sidebar. On a first opening of a large repository without
  a cache that is seconds.
- The cards' group of branches ready to remove, the review progress counts and the sessions' titles on cards go with
  nothing in their place.
- Which sections are open and which remotes show their merged branches is not saved; it starts over with each folder.
- A folder left on Work, Changes, Branches or a thread opens on HEAD's history.

### Neutral

- The table is named `gitWindowA`, so the sidebar opens at its own width rather than the one ImGui saved for the
  navigator's column.

## Alternatives Considered

- **Keep the navigator and give the branch table the sections.** Two places to look for one branch, and the table's
  trunk column needs the work facts this drops.
- **List the rows read so far while the lines are computed.** Rows would appear, move and change sections as the read
  lands, the drift the waiting row exists to prevent.
- **Map the old saved views onto the new window.** A card, a thread or the table has no counterpart; HEAD's history is
  what dropping the keys gives anyway.

## Related

- [0654](0654-count-a-branch-as-holding-work-while-a-commit-of-its-own-has-no-patch-twin-in-the-trunk-or-its-worktree-has-changes.md),
  [0657](0657-browse-every-branch-in-a-table-in-the-git-window-s-main-area.md),
  [0658](0658-open-the-git-window-on-a-navigator-of-work-changes-branches-and-stashes-beside-one-main-area.md): superseded.
- [0655](0655-start-a-branch-review-on-its-first-viewed-mark-and-keep-drafts-out-of-reviews-json.md): superseded in
  part, its thread view.
- [0633](0633-show-every-wait-with-the-core-loader-and-say-nothing-is-there-only-after-an-answer-said-so.md): the
  loader the waiting row uses.
- [0665](0665-keep-each-repository-s-git-answers-and-caches-in-local-user-data-keyed-by-its-shared-git-directory.md):
  where the lines the rows describe are cached.
- `docs/superpowers/specs/2026-11-03-git-window-lines-design.md`, sections 4.3 and 6.
