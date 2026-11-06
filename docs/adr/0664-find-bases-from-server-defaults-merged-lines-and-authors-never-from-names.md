# 0664. Find bases from server defaults, merged lines and authors, never from names

Status: proposed
Date: 2026-11-03

## Context

The line model ([0663](0663-compare-each-branch-with-the-line-it-forked-from.md)) needs the repository's bases: the
lines other work is compared with, such as the trunk and the release lines. Teams call them `main`, `master`,
`develop`, `release/1.2`, `next-16-3` or nothing recognisable, keep them as merge-based or squash-only histories, and
run them on a fork whose own `origin` is a stale copy. A rule keyed on a name works for the team that wrote it.

Two obvious rules fail. "A base is a branch three others fork from" gives 275 bases in one real repository and 416 in
another. Counting a line's own stretch without cutting it at commits a base already has turned five-year-old feature
branches into bases.

## Decision

Find the bases from the data, in `Core::ComputeLines`, with the thresholds named in `LineThresholds.h`.

- **Server defaults.** Each remote's default branch (its symbolic HEAD) is a base, unless it is an old copy. A default
  is an old copy only of a base on a different, non-empty remote that has at least as many refs as the default's own
  remote (counting every ref of each remote in the input), and whose first-parent chain holds the default's tip
  strictly below that base's tip; a colleague's remote with one integration branch built on the default's tip does
  not demote the default. The walk down that chain reads commit times as numbers and gives up a day below the
  default's commit time, so a badly skewed clock can hide an old copy but never make one. When two defaults from
  different remotes are at the same commit, the one whose remote has fewer refs becomes the old copy of the other;
  with as many refs on each, the one whose remote name sorts later does. Old copies are listed as such and never used.
  The test runs once, against every base, after the other rules have run. Of the bases a default copies, one takes
  over its role: the user's main line when it is one of them, else another remote's server default, else the one on
  the line that the most active branches are compared with, counted as the guess counts them (below) with nothing
  pinned, over the lines of those bases only; with as many votes, the one on the line that goes on from the default's
  tip with nothing pinned, then the one listed first. A heavier feature or release line built on the stale tip so
  never takes the main line from a default the upstream reports or from the user's answer, and a release line listed
  earlier does not take it from the line most work is compared with. That base moves into the default's place in the
  list, is pinned as the default was, and stands for the main line, so nothing is guessed; the default is named an
  old copy of it, the base whose line then holds the default's tip. The lines are then computed once more without the
  old copies. Old copies exist for forks, which are a matter of remotes and not of names: a fork whose `origin`
  reports a stale default finds the real main line from the upstream's default, the user's answer or the rules below,
  and asks no question.
- **The user's answers.** Bases the user added are bases; bases the user removed are not. The user's choice of the main
  line is a base.
- **Merged lines.** A line into whose own stretch (its commits up to where it joins another line) three or more
  different lines were merged in the last 90 days is a base. A second parent on a base's first-parent line does not
  count (trunk merged into one's own branch), and two second parents are one line when one lies on the other's chain.
  *Superseded by [0675](0675-count-a-line-s-own-work-committed-by-someone-else-toward-a-base-and-ask-merged-lines-for-two-people-or-a-week.md): the merged lines' tips must also have two or more authors, or the merges span seven
  days or more.*
- **Authors.** A line whose own stretch has five or more commits by three or more authors over seven days or more is a
  base. This finds release lines in teams that squash and leave no merges. *Superseded by [0675](0675-count-a-line-s-own-work-committed-by-someone-else-toward-a-base-and-ask-merged-lines-for-two-people-or-a-week.md): only
  the stretch's commits no other base reaches count, and only the authors of those that are no merge and that someone
  other than their author committed.*
- **Active and kept.** A base is active when its tip is within 90 days; the others are listed folded. A line found by
  the rules stays a base afterwards, stored with the repository's answers, until the user stops it or the branch is
  deleted. A base the user stopped is never kept again, not even by a read that found it and was still running when
  the user stopped it.
- **Order.** The bases are listed with the server defaults (in the order of the refs) or the main line first, then
  the bases the user added and those the rules found, in the order each first became a base, which the answers keep
  as `baseOrder`; last come the ones this computation found that the order lacks yet, in the order found. A read
  appends each base it finds to the order, and a name that is neither added nor kept any more leaves it. The order,
  not the kind of answer, decides where two bases collide in the split
  ([0663](0663-compare-each-branch-with-the-line-it-forked-from.md): a later-listed base on an earlier one's chain owns
  no line of its own until it has a commit of its own), so a base the user adds at the tip of a base kept before it
  leaves that base its line.
- **Bases before lines.** Bases are fixed before the lines are split, because the rules read lines and the lines
  are pinned to the bases. The split is run with the server defaults and the user's answers pinned, the lines that
  qualify are added as bases, and the split is run again, until a pass adds none. Real repositories settle in two
  passes; a cap of six keeps a pathological history from going round for ever.
- **No main line.** The model guesses only when there is no ServerDefault base, no MainLineByUser base and no base
  that a default set aside as an old copy copies: no remote default, a default whose tip is missing, a main-line
  answer naming a missing ref, or the only default removed by the user. Added and Kept bases do not stop the guess. The guess counts per line: each active branch votes for the line of the
  ref it is compared with in a split with no base pinned, branches at one commit vote once, and the guess is that
  line's first ref that the user has not removed, at its top. A removed ref passes its count to the next ref at the same
  commit, and when every top ref of the busiest line is removed the next line is tried. The guess is marked as guessed
  everywhere it is shown, and the window asks once which branch is the main line. The answer is stored per repository.
- No rule reads a name. Names are carried and compared for equality only; the one tie a remote name settles (two
  defaults at one commit on remotes with as many refs) compares names for order, not for meaning.

## Rationale

Merges and authors are what a base is: many pieces of work land in it, from many people, over time. A name says
nothing about that and a count of forks says too much. The spike found the right bases in every repository it was run
on without reading a name once, and where a team's setup defeats the rules, the user's answers remain.

The guess has to be a question because in a copy of this repository the plain heaviest line was a feature branch.

## Consequences

### Positive

- The same rules find gitflow's master and 2.0, a code editor's release lines, and the squash-only release lines of
  two other repositories, with a fork's stale default recognised as a copy.
- A quiet support line does not drop out of the bases, and so does not un-hide everything merged into it.

### Negative

- Agent and integration branches that many people merge into appear as bases while they are active. They are not wrong
  by the rules and the user can stop one; the window cannot tell them from a release line. *Narrowed by [0675](0675-count-a-line-s-own-work-committed-by-someone-else-toward-a-base-and-ask-merged-lines-for-two-people-or-a-week.md):
  one person's or one agent's branches, merged within a week, no longer make a base.*
- A repository with no remote default and no stated main line is asked once, and its guess is shown as a guess until
  then.
- A stale default in a fork whose upstream has fewer refs than the fork stays a base.

### Neutral

- Thresholds (three lines, three authors, five commits, seven days, 90 days) live in one header; changing one is a
  code change and not a setting.
- The order of the bases is history: two clones of one repository can list them differently, and so split a
  collision differently, when their bases were added or found in another order.

## Alternatives Considered

- **Bases by name patterns.** Lost: right for the team that wrote them, silently wrong for the next.
- **A base is a branch that several others fork from.** Lost: 275 and 416 bases in two real repositories.
- **Only server defaults and the user's list.** Lost: no release lines at all in repositories where the server knows
  only one default.

## Related

- [0663](0663-compare-each-branch-with-the-line-it-forked-from.md) uses the bases.
- [0665](0665-keep-each-repository-s-git-answers-and-caches-in-local-user-data-keyed-by-its-shared-git-directory.md)
  stores the user's answers and the kept bases.
- `docs/superpowers/specs/2026-11-03-git-window-lines-design.md`, sections 5.3 and 5.4.

Superseded in part by [0675](0675-count-a-line-s-own-work-committed-by-someone-else-toward-a-base-and-ask-merged-lines-for-two-people-or-a-week.md): the merged-lines and authors rules above. Server defaults, old copies, the
user's answers, kept bases, the order, bases before lines and the guess are unchanged.

## Evidence

The whole model was run on five real repositories, first in a throwaway script and then in C++ (the acceptance test
`Lines: acceptance on the repository in GRAPHITE_LINES_REPO`, skipped unless that variable is set). Active bases: a
gitflow project 3 (develop, 2.0, master) and its fork 3, with the fork's stale default found as a copy; a web
framework 8; a code editor 15 (main and its release lines); a team chat server 30. Backports to their release line: 5
of 5 in the framework, 96 of 112 in the chat server. The C++ figures equal the script's.
