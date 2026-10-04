# 0553. Keep the commit message readable in a narrow history and ask before every detached checkout

Status: proposed
Date: 2026-10-04

## Context

The commit history is a table of graph, hash, message, author and date. Docked
narrow, the fixed columns took the width and the message, the column a reader
actually scans, shrank to a few characters or to nothing.

Checking out a commit that no branch points at leaves HEAD detached. Commits
made there belong to no branch and are lost once the reader switches away,
which nothing in the panel said before it happened.

## Decision

- The graph column is capped to a quarter of the table. Then, while the message
  column is under its minimum width, the Author column is hidden first and the
  Date column second. The hash and the expand arrow always stay: a row is
  looked up and opened by them. Branch labels on a row are kept while the
  message keeps its minimum; the message is shortened before they go. The
  layout is computed by `LayoutCommitColumns`, from the widths the columns are
  actually drawn at, including a width the user dragged.
- Checking out a bare commit always asks first. The question says that HEAD
  will be on no branch and that commits made there are easy to lose, and offers
  Checkout or Create Branch Instead. A branch or a remote branch is checked out
  without that question, since it leaves HEAD on a branch.

## Rationale

The message is what tells one commit from another; the author and date are
secondary, and the author is the less often needed of the two. Asking before a
detached checkout costs one click on a rare action, where not asking can cost
work the reader does not notice is unreachable until later.

## Consequences

### Positive

- A history docked in a side column still shows readable messages.
- Nobody lands on a detached HEAD without having been told, and the branch they
  probably wanted is one button away.

### Negative

- In a narrow layout the author and date are not visible at all; the reader has
  to widen the panel or expand the commit to see them.
- Readers who check out commits often, to bisect by hand, answer the question
  every time.

### Neutral

- The overwrite question of 0539 is still asked after this one when the
  checkout would overwrite changes.

## Alternatives Considered

- **Shrink every column proportionally.** Lost because every column then
  becomes unreadable at once instead of the least needed ones going.
- **A "don't ask again" box on the detached question.** Lost for now: the
  action is rare enough that the question is cheap, and a forgotten setting is
  how work gets lost.

## Related

- [0539](0539-plan-every-checkout-and-ask-before-it-would-overwrite-work.md)
- [0541](0541-put-branch-and-stash-actions-in-the-git-panel-s-right-click-menus.md)
