# 0535. Show git state in the status bar read-only and keep git actions in the panels

Status: proposed
Date: 2026-10-01

## Context

The status bar is the one strip of the window that is always visible, and it
carried nothing about the repository: the branch, whether HEAD is detached, and
how many files had changed were only visible after opening the Changes or Git
panel. Git support grows in stages, and each stage that adds actions - branch
actions and the network among them, both outside this decision - could offer them
anywhere. A place has to be chosen for them once, or each stage will choose
again.

The status bar is drawn by a function that knows nothing of `GitModel`; it
receives the text to show. Its pieces are small, a click target is a few
characters wide, and an action started from it has no room to show a
confirmation, an error or a list of choices.

## Decision

- Draw a git segment at the left edge of the status bar: the branch (or the
  abbreviated commit when detached; followed by `(no commits)` when unborn),
  the distance from upstream as `↑N ↓M` or `✓` when the branch has an upstream
  and the distance is known, and the number of changed paths.
- Show an operation in progress (`MERGING`, `REBASING`, `CHERRY-PICKING`,
  `REVERTING`, `BISECTING`, `APPLYING`) after the branch, separated by a middle
  dot (`main · MERGING`), so the branch stays readable while it runs.
- When the folder is not a repository show `No repository`, greyed and inert;
  when no folder is open or the head state has not arrived, show nothing rather
  than a guess.
- Make the segment read-only. A click on the branch focuses the Git panel and a
  click on the changes count focuses the Changes panel; nothing in the bar
  changes the repository.
- Build the segment text in a pure function, `GitStatusText`, from the head
  state, the branches and the status, and hand it to the bar as data, so the bar
  stays free of `GitModel` and every row of the display is testable without a
  window.
- Keep every git action - stage, commit, branch, checkout, fetch - in the
  panels, on their own buttons and context menus.

Amended by [0620](0620-show-problems-and-the-server-s-health-as-glyphs-and-colours-in-the-status-bar.md):
the branch and the change count fill the full height of the bar under the
pointer. The change count stands 16 px from the branch instead of one font
size, and the path 16 px from the segment instead of two. Their wording and
their clicks are unchanged.

Amended by [0623](0623-show-the-file-s-path-and-the-symbol-at-the-caret-above-the-code.md):
the path no longer follows the git segment; it moved to the breadcrumb row above
the code.

## Rationale

A glance should answer "where am I and is anything changed", and a panel should
answer "what do I do about it". The bar has no room for feedback, so an action
started there would fail without a visible reason, and a stray click on a strip
the eye passes over all day would change the repository. Opening the panel on a
click keeps the bar useful as a shortcut without making it a control surface.

## Consequences

### Positive

- The branch, the distance from upstream and the change count are visible in
  every layout, whichever panels are open.
- The status bar and the panels cannot disagree about what an action did,
  because only the panels act.
- Later stages add their actions to the panels without revisiting the bar.

### Negative

- Switching branches takes two steps: a click to reach the Git panel, then the
  action. The branch name looks as if it could do more than it does.
- The distance behind upstream is only as fresh as the last fetch, which the
  IDE does not make itself, so the bar can show `✓` when the remote has moved
  on.

### Neutral

- The segment reuses the status and branch queries the panels already make,
  and adds one cheap head query, requested beside each status refresh.

## Alternatives Considered

- **Make the branch piece a menu that switches branches.** It would save a
  step, but a menu on a strip this small cannot show a refusal (a dirty tree
  that blocks the checkout) next to its cause, and it would give branch actions
  two homes.
- **Show nothing in the bar and leave the state to the panels.** It avoids the
  display work, but the state is then invisible exactly when the panels are
  closed, which is when a user most needs a reminder.

## Related

- [0524](0524-hold-change-markers-in-a-model-of-their-own-apart-from-gitmodel.md)
  - the status bar, like the change markers, reads git state through a model
  rather than the repository.
- [0536](0536-initialise-a-repository-without-asking-for-confirmation.md) - the
  one repository action the panels offer while no repository exists.
