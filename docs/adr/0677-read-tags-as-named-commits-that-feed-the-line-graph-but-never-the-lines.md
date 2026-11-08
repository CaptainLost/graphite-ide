# 0677. Read tags as named commits that feed the line graph but never the lines

Status: proposed
Date: 2026-11-08

## Context

The Git window listed branches and stashes only. A reader also asks two questions that tags answer: which release
holds this commit, and what was released when. Tags differ from branches in the ways that matter here:

- **Volume.** A web framework has 4,133 tags (3,707 annotated), a team chat server 1,614 (71 under `@mattermost/`), a
  code editor 401 (with `release/` and `translation/`), a CMS 408. The spec sets 5,000 tags beside 200,000 commits and
  6,000 branches as the scale to hold.
- **Shapes.** A tag is lightweight (a ref to a commit) or annotated (a tag object with a tagger, a date and a message);
  a tag can point at another tag, at a tree or at a blob; in a shallow clone its commit can be missing.
- **The line model must not change.** Bases and each branch's line come from branches alone
  ([0663](0663-compare-each-branch-with-the-line-it-forked-from.md),
  [0664](0664-find-bases-from-server-defaults-merged-lines-and-authors-never-from-names.md)). A tag is a name on one
  commit that never moves on, not a line, and nothing may read a name for meaning: a tag is not assumed to be a
  version.
- **Nothing appears out of nowhere** (spec §6): a section shows when its membership is known, a value computed later
  fills a reserved place, and a fetch that adds, moves or deletes tags applies at once.
- **The history only a tag reaches**, such as an old release whose branch was deleted, is in no branch's walk, so the
  graph read for the lines lacks it.

## Decision

Read tags as named commits: listed with the branches, their commits walked into the line read's graph beside the
branch tips, and never given to the line model.

- **A tag's date** is an annotated tag's tagger time, and the commit's committer time for a lightweight tag or an
  annotated one without a tagger. A tag of a tag takes the outermost tagger. The last case departs from git's
  `creatordate`, which is empty for an annotated tag without a tagger and sorts it as the oldest of all: the commit's
  date is kept instead, since a date of 1970 would read "made 56y ago" in the sidebar and the line. The listing is newest
  first, then by name (`Core::Repository::QueryTags`).
- **Tags are peeled to a commit.** Every `refs/tags/*` is followed through any number of tag objects. One that ends at
  a tree or a blob, one whose commit the object database lacks, and a symbolic ref under `refs/tags` are not listed. A
  missing object is no failure; any other read error fails the listing.
- **Listed in the branch-listing job**, after the branches, and landed in the same update, so the tags' membership is
  known exactly when the branches' is. A failed tag listing never holds the branches back: the branches land, the last
  good tags stay, and the error is kept apart (`GitModel::GetTagsError`). A `Core::TagCache`, per folder and never
  evicted, maps a tag object to its peeled commit and date: tag objects never change, so a listing after a fetch reads
  only the new ones. The same tags keep the same snapshot pointer.
- **Tag commits join the graph walk, never the lines.** `LineReadInput::Tags` adds each tag's commit to
  `GraphReadRequest::Tips`; the model is computed on the branches' refs alone, so its lines, bases, line per branch and
  states are those of a read without tags. `graph.bin` grows by the history only tags reach. A tag whose commit is gone
  since the listing (pruned in between) is passed over, as an unreadable branch tip is. A change of the tag snapshot
  alone asks a new read.
- **The first tag of every commit is part of the line read**: once the graph is final, one pass
  (`Core::OldestTagHolding`, O(commits + parents)) gives each commit the oldest tag whose commit reaches it, kept in
  `LineRead::FirstTag`. The tags are ranked oldest first; of two made in the same second the one whose name comes first
  ranks first, as git's `--sort=creatordate` with its name tie-break gives them. A commit outside the read's graph is
  held by no tag, which is exact: every listed tag's history is in the graph.
- **The Tags section** comes after the remote sections and before Stashes
  ([0669](0669-show-references-in-a-sidebar-beside-the-history.md)), closed by default; a search opens it.
  - A row is one line: the tag icon, the name, and the age (`FormatBranchAge`) on the right. Its tooltip holds the full
    name, the date, an annotated tag's subject and the commit's short hash.
  - A name with `/` goes into a folder named by its first segment (`@mattermost/`, `release/`), whose row stands where
    its newest tag is, shows its count and is closed by default; the tag's row shows the rest of the name. Folders nest
    one level only.
  - No section while the tags are unread, and none for a repository without tags. A failed listing shows the section
    with `couldn't read tags: <reason>`. The section and the folder holding the shown tag open, unless the user closed
    them.
- **Tag pills** are amber (`RefTagFill`, `RefTagText`), after the branch pills and newest first, on history rows and in
  the details' refs line.
- **The "First tag" line** of a commit's details comes right under In bases and the source sentence: the label `First
  tag`, then the tag's pill and `made <age> ago` (`made just now` while the age reads `now`), or `no tag holds it yet`,
  or `…` while no read is in hand or the read in hand was made from an older tag listing than the one shown (compared
  by pointer, as In bases waits for a commit outside the read), or a dim `unknown`, with the reason in its tooltip,
  when either wait has no answer coming: the line read failed, or, with no read held or coming, the branch listing
  did, as In bases says it. It is one line high in every state. It follows In bases: it is drawn wherever In
  bases is, a stash's details included, whenever the repository has tags. Whether it has is the tag listing's answer
  once the listing is in, so a fetch that deletes every tag takes the line away at once; before that, the read's.
- **Window state.** A tag's full name restores its history. The restore waits for the tag listing, and a name no longer
  listed falls back to HEAD's history.

## Rationale

Tags answer "which release holds this commit" only if their history is in the graph, and they would distort the bases
and lines if they were refs of the model; feeding their commits to the walk alone gives the first answer without the
second. Listing them in the branches' job makes one moment at which the sidebar's membership is known, and answering
the first tag of every commit in the read job costs one linear pass instead of a walk per selected commit, so the
details never wait on a job of their own.

## Consequences

### Positive

- On the four acceptance repositories the model with tag commits in the graph equals the model without them, field by
  field, from a first read and from `graph.bin`.
- The first tag of 800 sampled commits equals `git for-each-ref --contains <c> --sort=creatordate --count=1 refs/tags`,
  five of them ties of the same second that both break by name.
- The first-tag pass costs 0.17 ms on the web framework's 50,808 commits and 0.78 ms on the code editor's 196,763
  (Release). First tag is part of the read, so it lands in the frame the read lands, with no job of its own.
- The sidebar's rows for 4,133 tags, the section open, build in under 1 ms, and a search over them in under 2 ms.

### Negative

- A tag that ends at a tree or a blob, or whose commit is missing, is invisible.
- Every branch listing waits for the tag listing: 3 ms on the web framework with the cache, 45 ms on the first listing
  of a session, which delays the branches, and the line read behind them, by as much.
- A first read without `graph.bin` reads the history only tags reach (161 to 620 commits on the four repositories) and
  asks for each tag commit the graph lacks whether git holds it: up to about 37 ms more.
- A fetch that only adds, moves or deletes tags reruns the whole line read on a worker (11 ms on the web framework, 130
  ms on the code editor) and hands a new read to every follower; on the UI thread that is under 5 ms. While it runs, a
  commit outside the read says `…` in In bases, since the new read may hold it.
- A commit only a tag reaches is now in the read's graph, so at an opening its In bases reads `…` until the read and
  its bases job land (about 0.75 s on the web framework), as every other commit in the graph already does: no model is
  kept on disk. Before tags were read, such a commit was outside the graph and said `in no base yet` at once.
- Folders nest one level only, so a deeper name such as `release/1.x/1.2` shows `1.x/1.2` in its row of the `release/`
  folder.

### Neutral

- Tags have no context menu; checking out a tag (a detached HEAD) is later work.
- The tag cache lives in memory only, so each session's first listing reads every tag object once.

## Alternatives Considered

- **Make tags refs of the line model.** The model would have to tell tag refs from branch refs at every step, and a
  tag commit on a merged line could change which lines are bases by the merged-lines and authors rules; the spec forbids
  that change.
- **Answer the first tag per selected commit** (asking each tag, oldest first, whether it reaches the commit, when the
  details open). Every selection would wait on a job of its own, and the line would fill in after the pane is drawn;
  on the web framework that is up to 4,133 reachability questions per selection, where one pass answers every commit
  in 0.17 ms.
- **List tags in a job of their own.** The Tags section's membership and the branches' would land at different
  moments, so the sidebar would change twice per refresh; and the restore of a tag would wait on a second listing.

## Related

- [0663](0663-compare-each-branch-with-the-line-it-forked-from.md): the line model the tags leave unchanged.
- [0669](0669-show-references-in-a-sidebar-beside-the-history.md): the sidebar the Tags section joins.
- [0672](0672-open-a-branch-s-history-on-its-own-commits-with-its-line-s-newer-commits-folded-into-one-row.md): a tag's
  history, shown combined.
- [0665](0665-keep-each-repository-s-git-answers-and-caches-in-local-user-data-keyed-by-its-shared-git-directory.md):
  where `graph.bin` is kept.
- `docs/superpowers/specs/2026-11-03-git-window-lines-design.md`, sections 4.3, 4.4, 4.6, 6 and 8.
