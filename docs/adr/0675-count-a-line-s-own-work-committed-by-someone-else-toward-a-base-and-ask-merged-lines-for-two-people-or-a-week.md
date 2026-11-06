# 0675. Count a line's own work committed by someone else toward a base, and ask merged lines for two people or a week

Status: proposed
Date: 2026-11-06

## Context

[0664](0664-find-bases-from-server-defaults-merged-lines-and-authors-never-from-names.md) finds the bases a branch is
compared with from the data: a line is a base when three or more lines were merged into its own stretch in 90 days,
or when its stretch has five or more commits by three or more authors over seven days or more. On large repositories
those rules found 146 bases in a code editor and 197 in a team chat server, many of them topic branches: 37 and 84
that are no release line, 6 and 23 of them active and so listed unfolded and offered in the picker.

An investigation of every one of them found four causes:

- **Authors of merge commits.** A reviewer or the hosting service bringing a topic up to date with the trunk, or the
  same person under a second e-mail, is the third author of a two-person topic.
- **History a base already has.** A line's own stretch is first-parent only, but it can still run through hundreds of
  commits the trunk holds through a merge's second parent: a topic built on a pull request the trunk merged, or a
  branch made from an old trunk that a merge left as the trunk's second parent.
- **One person's merges.** An agent merging three of its own sub-branches into an integration branch within a day
  passes the merge rule.
- **Three or four identities.** A developer under two e-mails with a coding agent, all pushing their own commits. An
  author count cannot tell this from a three-person release line; who committed the work can. The release lines' own
  commits are committed by someone other than their author (the hosting service merging a pull request, a release
  manager picking it) in most cases, a median of 55 to 100 % across three repositories, while the topics' median is 6
  to 31 %.

No rule may read a name, and the floors the line model was accepted on must hold: backports compared with their
release line 96 of 112 in the chat server and 5 of 5 in a web framework, every active release line of the editor a
base, a gitflow project's develop, 2.0 and master, and a fork's stale default found as an old copy.

## Decision

In `Core::MeasureLine` (`LineMeasure.cpp`), with the thresholds in `LineThresholds.h`:

- **A line's own commits** are the commits of its stretch that no other base reaches. While the rules look for bases,
  every line measured is no base's, so one walk from all base tips per pass (`CommitsReachableFrom`) gives the cut.
  When the bases are described afterwards, each base's line is cut at what the other bases that own a line reach,
  found for all of them in one children-first pass that keeps up to two reaching base tips per commit; a base whose
  tip sits on another base's line reaches nothing its owner lacks and cuts nothing.
- **The author rule** counts the distinct authors of the own commits that are no merge and that someone other than
  their author committed: the committer's e-mail key (`AuthorKeyOf`) differs from the author's. The span and the
  commit count are taken over all own commits, merges included, since merges keep a release line going over time.
  Five commits, three authors and seven days stay the thresholds.
- **The merge rule** still needs three or more distinct lines merged into the stretch in 90 days, counted as before
  over the whole stretch, and now also that those merges' merged tips have two or more authors
  (`kMergedAuthorsForBase`) or that the merges span seven days or more (`kSpanForBase`).
- **The graph** carries one bit per commit, `GraphCommit::CommittedByAuthor`, read from libgit2's committer. The cache
  file `graph.bin` keeps it in bit 1 of the per-commit flags byte and moves to format 2 (magic `GLG2`); a format 1
  file is not loaded, so the graph is read from git again once. Without that, every cached commit would count as
  committed by its author and the author rule would find nothing.
- **Kept bases stay as they are**: a line once found stays a base until the user stops it or the branch is deleted.
  There is no rules version in `answers.json` and no one-time drop of bases the old rules found. A kept base whose
  line no longer qualifies is described as kept; one whose line still qualifies reports the rule.
- **The main-line guess** keeps weighing tied lines by every author of the stretch, merges and self-committed work
  included: it runs where no remote reports a default, mostly in a repository whose authors commit their own work,
  where the author rule's count would be nought on every line.

## Rationale

What makes a release line a base is other people's work landing on it, and the commit records who landed it. Cutting
at what a base already holds measures the line's work instead of the history it carries. Asking merges for a second
person or a week keeps gitflow's master and 2.0, merged into by one maintainer over weeks, and every editor release
line, merged into by several people, while one agent's burst of merges no longer counts. Measured on six
repositories, this removes 33 of 37 and 61 of 84 topic bases and moves no floor.

## Consequences

### Positive

- Active topic bases fall from 6 to 0 in the editor, 23 to 4 in the chat server and 5 to 1 in the web framework; the
  bases left are the trunks and release lines, a release line still carried under a pull request's name (the split's
  weak spot), and integration lines into which other people's branches are merged.
- The warm read is as fast as before: one reach walk per pass and one children-first pass when describing, never a
  walk per base.

### Negative

- Quiet release lines whose own work was merged back into the trunk, or whose picks their own authors pushed, become
  ordinary branches compared with the line they forked from: 19 editor lines from 2017–2019, 15 chat server lines
  from 2016–2019 and two of the gitflow project's maintenance lines. No active release line is lost.
- A team whose shared line only receives commits pushed by their own authors, with no merges into it, is found by
  neither rule; Use as base covers it.
- Stores written by earlier builds keep the bases the old rules found until the user stops them or the branches go,
  because no migration is written for development builds. Their cached graph is read from git once more.
- A base's description counts only the work no other base reaches, so a base from which another base's line forks
  describes fewer commits, and fewer people, than its whole line holds.
- Six quiet topics rebased onto the trunk still pass: the rebase copied other people's trunk commits, which the person
  rebasing committed. Telling a copy from new work would need the author time, a second cache change.

### Neutral

- `kMergedAuthorsForBase` joins the named thresholds; the merge span reuses `kSpanForBase`.
- The sidebar's "N people worked on it over D days" now counts the own work's people; the wording is unchanged.

## Alternatives Considered

- **Count four or five authors.** Lost: removes fewer topics than the committer test and drops real release lines in
  the chat server and the editor.
- **Count only a line that branches are forked from.** Lost: about fifty release lines per large repository drop, and
  the chat server's backports fall below their floor (95 of 112).
- **One person per author name.** Lost: needs names in the cache and keeps more topics than the committer test.
- **Keep a base only while it is active.** Lost: no active topic base goes, and quiet release lines found only by the
  90-day merge rule drop out four months after their last commit, which "kept" exists to prevent.
- **A rules version in `answers.json` that drops the bases the old rules found.** Lost for now: it is a migration, and
  the house rule writes none for development builds.

## Related

- [0664](0664-find-bases-from-server-defaults-merged-lines-and-authors-never-from-names.md): its merged-lines and
  authors rules are superseded by this record; the rest stands.
- [0663](0663-compare-each-branch-with-the-line-it-forked-from.md): the lines and the split the rules measure.
- [0665](0665-keep-each-repository-s-git-answers-and-caches-in-local-user-data-keyed-by-its-shared-git-directory.md):
  the stores holding the kept bases and `graph.bin`.
- `docs/superpowers/specs/2026-11-03-git-window-lines-design.md`, sections 5.3 and 10.

## Evidence

The acceptance harness (`Lines: acceptance on the repository in GRAPHITE_LINES_REPO`), on fresh stores, a cold and a
warm read, gives bases total / active (topic bases total / active):

| repository | before | after |
|---|---|---|
| gitflow project | 6 / 3 (1 / 0) | 3 / 3 (0 / 0) |
| its fork | 6 / 3 (1 / 0) | 3 / 3 (0 / 0), stale default still an old copy |
| this repository | 1 / 1 | 1 / 1 |
| team chat server | 197 / 30 (84 / 23) | 121 / 11 (23 / 4), backports 96 / 112 |
| web framework | 24 / 8 (9 / 5) | 17 / 4 (2 / 1), backports 5 / 5 |
| code editor | 146 / 14 (37 / 6) | 94 / 8 (4 / 0), main and 7 active release lines |

Warm read, Release build, two runs each against the build before: editor 265 / 261 ms against 273 / 267 ms, chat
server 46 / 48 ms against 53 / 55 ms, web framework 40 / 40 ms against 45 / 43 ms.
