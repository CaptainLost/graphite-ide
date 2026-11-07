# 0676. Find branches merged by content in the background and keep the verdicts per repository

Status: proposed
Date: 2026-11-06

## Context

The line model ([0663](0663-compare-each-branch-with-the-line-it-forked-from.md)) calls a branch merged when its tip
is reachable from its line's tip. A branch whose pull request was squashed, rebased or cherry-picked onto its line is
not reachable from it, so it stays "not merged" for ever: on large repositories most branches merged that way read as
open work. Only the commits' contents can tell: merging the branch into its line changes nothing (a squash), or every
commit of its own has a copy with the same patch on the line (a rebase or a cherry-pick). The spike found the two
tests catch different branches (on 95 cherry-picked branches of a team chat server, 17 by patch, 13 by merge, 23
together).

The forces:

- **Volume.** Under [0675](0675-count-a-line-s-own-work-committed-by-someone-else-toward-a-base-and-ask-merged-lines-for-two-people-or-a-week.md)'s
  rules a web framework has 2,807 branches to check, the chat server 1,762 and a code editor 5,536. A check costs from
  under a millisecond to seconds; the lines read, the log and the UI share one job queue.
- **The user's repository is not ours.** A merge in libgit2 writes blobs and trees into the object database; a check
  must leave no object, ref or file behind.
- **Partial clones.** A blobless clone lacks most blobs, and libgit2 does not fetch them; a check that needs one cannot
  answer, and that is not an error to show.
- **Nothing appears out of nowhere** (spec §6): a value computed later fills a reserved place and never changes a
  row's height, order or presence; a branch found merged keeps its row until the list is next rebuilt; the second
  opening is complete in its first frame.
- **Lines move on.** A squash found against one line tip can stop passing the merge test once the line later edits the
  same lines, although the branch is as merged as it was.

## Decision

Check every branch that history does not show merged for a merge by content, in the background, one check at a time,
and keep the verdicts per repository.

- **The check** is `Core::CheckMergedByContent` (`Core/src/Core/Git/Content`), on a branch tip and its line's tip:
  1. Either tip missing from the object database: *unknown*. No common history: not merged.
  2. The branch's tree equal to the merge base's (its changes cancel out): not merged.
  3. **The in-memory merge first** (`Repository::MergeChangesNothing`): `git_merge_commits` with
     `GIT_MERGE_FAIL_ON_CONFLICT | GIT_MERGE_SKIP_REUC` and rename detection off; the merged index is compared entry by
     entry with the line tip's tree (`git_diff_tree_to_index`), so no tree is written and a blob only the line changed
     need not be present. Yes, no (a conflict or another tree) or unknown (a missing object).
  4. **Then patch equivalence** (`Repository::MatchPatches`): each own commit's patch id against the line's commits
     since the fork, newest first, at most `kPatchCommitsPerCheck` = 3,000 line commits.
  5. **Rebase** when every change of the branch's own has a copy on the line, whatever the merge said (a cherry-picked
     branch reads `merged · rebase`); else **squash** when the merge changes nothing; else *unknown* when the merge
     could not answer, a patch hit a missing object, or the patch budget ran out; else not merged.
- **No object reaches the repository.** Checks run on a handle opened with `ObjectWrites::InMemory`: a `git_mempack`
  backend at priority 1000 takes every write the merge makes, and is emptied after each check
  (`DropObjectsInMemory`). The port keeps these handles apart from its read handles, one per worker thread.
- **The landing of a squash** is the oldest commit on the line's first-parent chain since the fork that changes a path
  the branch changes and into which merging the branch changes nothing. The chain is read lazily, newest first, and
  stops where the fork's history is reached; each chain commit's diff with its first parent is kept to the branch's
  paths, taken as written (`GIT_DIFF_DISABLE_PATHSPEC_MATCH`), so only the trees on those paths are read. Past
  `kPatchCommitsPerCheck` chain commits, or after `kLandingTries` = 64 merges, or at a candidate the merge cannot judge,
  the squash has no landing. A squash without a landing is still merged.
- **One check at a time, the shown branch first** (`Graphite::ContentModel`, `Graphite/src/Git/Content`). Owned by the
  layer, beside the lines model and declared before the job queue; driven by the Git window's update. A job starts
  only while the window is on screen and none runs, across a folder switch too. Each pick takes, in order: the batch
  rechecking remembered verdicts, the shown branch, the first slice of a copy search the details asked for, the rows
  drawn this frame, the next slice of a copy search under way, then every other candidate — not merged, compared with
  a line, with commits of its own — the user's own branches, then remote branches, the newest tip first in each. A row, the shown branch or a copy is asked again every
  frame it is drawn; an ask not repeated is dropped.
- **The folder guard.** Every job and delivery carries the folder it was asked for and is dropped once the folder has
  changed; the port refuses a call for a repository generation it no longer opens (`Cancelled`), which is asked again
  rather than recorded.
- **Reserved places** (spec §6 rules 2–3): see [0669](0669-show-references-in-a-sidebar-beside-the-history.md) for the
  sidebar's mark and its hiding at the next rebuild and
  [0672](0672-open-a-branch-s-history-on-its-own-commits-with-its-line-s-newer-commits-folded-into-one-row.md) for the
  header, the banner and the history's wait. In the details, a base that does not hold the commit by history but holds
  its copy is a dim chip `<base> ≈ copy`, the shown branch's squash `<base> ≈ in squash <hash>`, with `…` at the end of
  the line while a copy is still searched for; a landing or a copy says `Squash of <branch>.` / `Copy of <hash> from
  <branch>.` under the line, decided when the commit is selected. Copies are searched per selected commit, lazily, in
  each base that does not hold it, in slices: each job reads at most `kPatchCommitsPerCopySlice` = 100 more line
  commits (budgets 100, 200, … up to `kPatchCommitsPerCheck`), the patch ids read before coming from the cache, until
  a copy, the end of the line or the whole budget. A sliced search answers as one search over the whole budget would,
  and the `…` stays until its last slice.
- **The cache** (spec §7), beside `answers.json` in the repository's store
  ([0665](0665-keep-each-repository-s-git-answers-and-caches-in-local-user-data-keyed-by-its-shared-git-directory.md)):
  - `content.json` (`"version": 1`): the verdicts by (branch tip, line tip), merged, not merged or unknown, and per
    branch tip the first merged verdict found with the line's full name (`Found`). Read on the UI thread the first time a store is followed, so the
    second opening's marks are there in its first frame. Written by a job at most every five seconds while something
    changed, on a folder switch and on exit, holding only what the current read can use. A save asked earlier never
    writes over a later one of the same file.
  - `patches.bin` (magic `GPI1`): patch ids by commit, read by the first job that needs them, never on the UI thread.
  - A damaged file, or one of another version, is ignored and rebuilt: both are machine caches.
- **Remembered verdicts.** A `Found` verdict counts, unchecked, for a new tip of the line while the read's graph holds
  its landing (a squash) or every copy (a rebase) from that tip; a squash without a landing is never held. Every such
  branch of a new read is answered by one job, one graph walk per distinct line tip; the rest are checked again.
- **Unknown answers and failures.** A check that cannot answer (*unknown*: a tip or an object it needs is missing, or
  the line ran past the patch budget after the merge said no) reads as unknown: no mark, nothing in the window bar. It
  is kept for its pair like any other verdict, so the second opening knows it in its first frame: the same two tips
  give the same answer. A check that fails reads as unknown too, is logged once, is not asked again in the session,
  is never written to disk and is asked again in the next session.

## Rationale

The merge is one bounded operation per branch, while the patch walk grows with the line's commits since the fork, so
the merge runs first and the patch walk only decides the kind. An in-memory object backend is the one way libgit2's
merge can run without writing into the repository. One check at a time keeps the shared queue free for the lines read
and the log, the shown branch's place at the head keeps its header waiting no longer than one job, and remembering the
landing keeps a branch merged after its line moved on.

## Consequences

### Positive

- Branches squashed, rebased or cherry-picked onto their line are marked and, at the next rebuild, hidden in the remote
  sections like branches merged by history.
- The repository is never written to; a blobless clone answers *unknown* where it cannot read, and says nothing.
- The second opening shows every verdict found before in its first frame; a moved line costs one graph job, not the
  checks again.

### Negative

- A first opening checks every candidate, the marks filling in as they land: 6 minutes of background work on the web
  framework, 109 on the code editor, where an old branch's check walks the 100,000-odd commits it lacks. Checks stop
  while the window is off screen, and a later opening asks only what changed.
- The shown branch's header waits for any job running when it is asked: one check, one slice of a copy search or one
  recheck batch (see Evidence for how long each takes). A sliced search walks again, for every slice, the headers of
  the line commits the commit lacks, and its copy is found a little later than in one job.
- In a blobless clone nearly every check answers *unknown*: the first opening spends minutes finding out what it
  cannot read, and a pair stays unknown after a git command fetched its objects, until a tip moves.
- `content.json` is parsed on the UI thread once per store; it holds a verdict per candidate.
- An unknown kept because the line ran past the patch budget records no budget: a later build that raises
  `kPatchCommitsPerCheck` does not ask that pair again until either tip moves.
- A squash whose landing lies past the chain budget, or behind a candidate that cannot be judged, is merged without a
  landing: the banner and the details name no commit.
- A branch whose changes cancel out is not merged by content, although merging it would change nothing.
- Branch deletion stays on merged-by-history: `git branch -d` refuses a branch merged only by content.

### Neutral

- The thresholds are named in `Core/src/Core/Git/Content/ContentThresholds.h`.
- Rename detection is off in the merge: a squash of a rename is the same delete and add on both sides.

## Alternatives Considered

- **Cheapest test first.** Lost: the patch walk is not cheap on an old branch (it reads patch ids up to its budget),
  and would still need the merge to find a squash.
- **`git merge-tree` through the git command line**, as the spike did. Lost: a process per branch for thousands of
  branches, and git fetches missing blobs of a partial clone from the network on its own.
- **Write the merge into the repository's object database and prune later.** Lost: the user's repository gains objects
  a later `git gc` must clean, and a crash leaves them.
- **Check only the rows in view.** Lost: the hidden count and a search could not tell a merged remote branch from an
  open one until each had been scrolled to, so the same list would hide different branches from one opening to the
  next.
- **Never keep an unknown answer, and ask it again in every session.** Lost: the second opening of a blobless clone
  showed `…` again on nearly every candidate's row (2,755 of 2,807 in the web framework) for the minutes the checks
  take, against spec §6 rule 5. The cost of keeping it: a pair whose missing objects a later git command fetches
  stays unknown until either tip moves.

## Related

- [0663](0663-compare-each-branch-with-the-line-it-forked-from.md): the lines and the states the check starts from.
- [0669](0669-show-references-in-a-sidebar-beside-the-history.md): the sidebar's mark and hiding.
- [0672](0672-open-a-branch-s-history-on-its-own-commits-with-its-line-s-newer-commits-folded-into-one-row.md): the
  header, the banner and the wait.
- [0665](0665-keep-each-repository-s-git-answers-and-caches-in-local-user-data-keyed-by-its-shared-git-directory.md):
  the store the cache files sit in.
- `docs/superpowers/specs/2026-11-03-git-window-lines-design.md`, sections 5.6, 5.7, 6 and 7.

## Evidence

The acceptance harness (`Lines: acceptance on the repository in GRAPHITE_LINES_REPO` with `GRAPHITE_CONTENT=1`)
checks every candidate in the window's order with one patch id cache, Release build, on the spike's clones. Those are
blobless, so most checks answer *unknown* for a missing blob; a full clone of libgit2 shows the full path.

| repository | checks | merged (squash with / without landing, rebase) | not merged | unknown | total | median / 99th | slowest |
|---|---|---|---|---|---|---|---|
| web framework (blobless) | 2,807 | 14 / 0, 0 | 38 | 2,755 | 443 s | 108 / 295 ms | 28.5 s, before the landing fix |
| team chat server (blobless) | 1,762 | 46 / 6, 0 | 24 | 1,686 | 121 s | 50 / 193 ms | 1.3 s |
| code editor (blobless) | 5,536 | 31 / 12, 0 | 223 | 5,270 | 6,551 s | 1,090 / 3,203 ms | 4.0 s |
| libgit2 (full clone) | 109 | 3 / 0, 2 | 65 | 39 | 27 s | 133 / 1,748 ms | 3.0 s |

- **The landing search** diffed each first-parent line commit since the fork with its whole tree, about 10 ms a commit
  on the web framework's tree: 30.6 s for a squash forked 2,887 line commits back. Kept to the branch's paths, the same
  searches take 0.30 s, 0.13 s and 0.07 s for chains of 2,887, 2,694 and 1,101 commits, finding the same candidates.
- **The patch walk's header walk before its budget**, timed with a budget of nought: 134 ms for a branch 27,134 line
  commits behind (web framework), 92 ms for 16,722 (chat server), 1.97 s for 166,740 (code editor), where it is half of
  a 4 s check: with the merge base, the walks over the commits a branch lacks are most of an old branch's check.
- **The recheck of remembered verdicts** after a line moves: 0.3 ms for 14 verdicts over 4 line tips, 1.8 ms for 52
  over 34, 16 ms for 43 over 28.
- **`content.json` on the UI thread**: at a verdict per candidate, 1,749 verdicts and 313 KB parse in 2.7 ms and the
  first `Follow` takes 3.4 ms (chat server); 5,520 verdicts and 947 KB parse in 8.3 ms and the first `Follow` takes
  11.0 ms (code editor): under a frame, so the file stays on the UI thread.
- **A copy search** on the full clone took up to 12.9 s in one job with no patch id known (8.1 s with the run's): an
  old release line's commit searched for in a newer one reads the patch ids of 3,000 line commits, each a diff with
  its files, and the shown branch's header waited for the whole search. In slices the longest single job is 1.73 s
  (about 0.33 s on average: 10.0 s over 30 slices), so the header waits for one slice; the 40 sliced searches answered
  as the unsliced ones did.
- On the full clone 39 of 109 checks answer *unknown*: old branches whose merge says no and whose line ran past the
  patch budget.
- **The two spike branches the in-memory merge called merged by content** in the web framework (`update/react/…`)
  have trees equal to their merge base's: their six commits cancel out, so they are not merged here, by design. The
  branch git found merged and every commit copied is merged (a squash here: the patch walk stops at a missing blob
  before the copy); the other branch git found every commit copied answers *unknown* for a missing blob.
