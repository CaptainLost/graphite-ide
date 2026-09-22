# 0514. Expand an untracked directory by asking git about that directory alone

Status: proposed
Date: 2026-09-22

## Context

`QueryStatus` asks libgit2 with `GIT_STATUS_OPT_INCLUDE_UNTRACKED` and without
`RECURSE_UNTRACKED_DIRS`, so a directory git has never seen arrives as a single
entry whose reported path ends in a separator — `Claude outputs/`. That is not
an accident of the flags; it is git's own answer to the question the panel is
asking. Nothing under such a directory is tracked, so there is nothing to say
about the files individually that is not already said by naming the directory,
and a working tree with a build output or a dependency cache in it stays one
row instead of ten thousand.

The changes panel then wants the opposite for one directory at a time: the
reader clicks the disclosure triangle on that row and expects the files under
it. The entry the status delivered cannot answer that — it is one path and no
children — so something has to produce the listing, and the choice of what is
the decision recorded here.

## Decision

The main status query keeps the collapsing. `QueryStatus` is unchanged: an
untracked directory is one entry, marked by `FileStatus::IsDirectory`, with the
trailing separator stripped from `Path` so `filename()` names the directory
rather than answering empty.

A caller that wants that directory opened up asks a second, separate query:

```cpp
Result<WorkingTreeStatus> Repository::QueryUntrackedIn(
    const std::filesystem::path& directory) const;
```

It builds a `git_status_list` with `GIT_STATUS_SHOW_WORKDIR_ONLY` and the flags
`INCLUDE_UNTRACKED | RECURSE_UNTRACKED_DIRS | SORT_CASE_INSENSITIVELY |
DISABLE_PATHSPEC_MATCH`, with `options.pathspec` set to that one directory, and
keeps only the entries carrying `GIT_STATUS_WT_NEW`. Paths come back relative
to the working directory, like every other status path.

`DISABLE_PATHSPEC_MATCH` is part of the decision, not a detail of it. Without
it libgit2 reads the pathspec as an fnmatch-style glob, and a directory whose
name contains `[` or `]` — perfectly legal on Windows, where `*` and `?` are
not — has its brackets read as a character class and matches nothing. The
expansion would then silently report an empty directory rather than failing.

It is a read, so it travels the same path `RequestBranches` and `RequestLog`
take rather than the serialised write queue. `GitModel` caches one listing per
expanded directory and drops every one of them when a fresh status arrives,
because what is untracked under a directory changes as the working tree does.

## Rationale

The two questions are genuinely different and deserve different queries. "What
is the state of this working tree" is asked on every file change, for the whole
tree, and its right answer for a wholly untracked directory is the directory.
"What is under this directory" is asked once, by a reader who clicked, about
one path. Keeping them separate means the cost of the second is paid only by
the directory that was opened, and the first keeps the fact — *this whole
directory is untracked* — that the second necessarily discards.

Asking git rather than the file system is the same rule
[0504](0504-treat-an-untracked-file-as-an-added-file-in-a-working-tree-diff.md)
was written for: a listing the reader can act on must contain exactly what the
action will accept.

## Consequences

### Positive

- An untracked directory is one row until someone opens it, so a large
  untracked tree cannot flood the list, and the cost of recursion is paid per
  opened directory rather than per status refresh.
- The listing agrees with `git add`: git applies the same ignore rules to this
  query as to every other, including a `.gitignore` nested inside the untracked
  directory.
- A directory whose name contains a bracket expands like any other.

### Negative

- Two queries mean two code paths that must stay consistent about what an
  untracked entry is. `QueryUntrackedIn` fills in less of `FileStatus` than
  `QueryStatus` does — it sets `WorkTree` to `New` and `IsUntracked`, and
  nothing else — and a field added to `FileStatus` for the panel's benefit will
  need adding in both places or the children will quietly lack it.
- Expanding a directory is asynchronous. There is a frame, or many, in which
  the row is open and has no children to draw, and the panel has to have an
  honest answer for that state rather than a number it does not have yet.
- The listings are cached per directory and thrown away wholesale on every
  status refresh, which in a working tree being written to means re-asking for
  every open directory repeatedly.

### Neutral

- Nothing is requested for a directory nobody has expanded, so a repository
  whose panel is never touched pays nothing for this at all.
- `QueryUntrackedIn` answers about untracked files only. A directory that is
  not untracked reports nothing, which is correct: its tracked contents are
  already individual entries in the main status.

## Alternatives Considered

- **Add `RECURSE_UNTRACKED_DIRS` to `QueryStatus`.** One query, no cache, no
  asynchronous half-open row. Rejected for two reasons. The first is cost: the
  status is re-asked after every change the watcher reports, and recursing
  makes a working tree with a large untracked directory in it expensive to ask
  about at all — the case ADR 0504 already names for the unfiltered diff.
  The second matters more: after recursion the fact is gone. Git names only the
  files, and "this whole directory is untracked" — the thing the row is
  supposed to say, the thing that makes one stage click stage the lot — can no
  longer be read off the answer.
- **Read the directory off disk with `std::filesystem`.** Immediate, synchronous
  and free of libgit2. Rejected because it would lie. A `.gitignore` nested
  inside an untracked directory is honoured by git and invisible to a directory
  walk, so the listing would show files `git add` will refuse, and a row the
  reader can click to stage something that will not be staged is the same
  failure ADR 0504 was written about. The file system also knows nothing about
  the global or per-repository ignore files, and reimplementing the matching
  would mean maintaining a second, worse copy of git's rules.

## Related

- [0504](0504-treat-an-untracked-file-as-an-added-file-in-a-working-tree-diff.md)
  — a listing must contain what the action on it will accept; the reason the
  listing is asked of git rather than of the disk.
- [0002](0002-keep-core-free-of-ide-knowledge.md) — `QueryUntrackedIn` answers
  about a directory, not about a panel row; the expansion state stays in the
  application.
- [0515](0515-ask-before-every-discard-in-the-changes-panel.md) — the folder row
  this expansion produces carries a discard, and what it throws away is what
  that record is about.
