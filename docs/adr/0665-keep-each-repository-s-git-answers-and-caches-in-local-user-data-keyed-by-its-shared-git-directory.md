# 0665. Keep each repository's git answers and caches in local user data, keyed by its shared git directory

Status: proposed
Date: 2026-11-03

## Context

The line model ([0663](0663-compare-each-branch-with-the-line-it-forked-from.md),
[0664](0664-find-bases-from-server-defaults-merged-lines-and-authors-never-from-names.md)) has two kinds of state that
outlive a run. The user's answers: bases added and removed, the main line, a branch's chosen comparison, and the bases
that stay bases once found. And a cache of git facts, the ancestry graph, which for a repository of 200,000 commits
costs about two seconds to read from git and a fraction of that to load.

The window's view state is kept per opened folder, in `.graphite/workspace.json`. A repository is not a folder: it has
several working trees, and agents create many. The answer to "which branch is the main line" belongs to the
repository, and the cache is machine-local and large.

## Decision

Keep both in Graphite's per-user data, in a folder per repository, named by the repository's shared git directory.

- The folder is `<root>/<16 lowercase hex digits>`, the FNV-1a 64 hash of the UTF-8 text of the repository's common
  directory (`Repository::CommonDirectory`, the directory every worktree shares), made absolute, lexically
  normalised, without a trailing separator, and lowercased, so every spelling of one directory gives one folder.
- It holds `answers.json` (versioned: the user's answers and the kept bases) and `graph.bin` (the cached graph).
- Every worktree of a repository reads and writes the same folder, so the main-line question is asked once per
  repository.
- Nothing is written into the repository or a working tree. Reading a repository never changes it.
- An incremental read asks git only for what the cache lacks: it hides the cache's own heads (every cached commit that
  no other cached commit has as a parent), not the current branch tips, so a branch that moved does not make the
  commits below its old tip be read again. A cached head git no longer has (pruned after its branch was deleted) is
  passed over, and what only it would have hidden is read again; only if the read still fails is the cache discarded
  and the whole graph read once. A branch whose tip git cannot read as a commit (a missing object, or an object that is
  no commit) is not read and is reported as such; it never fails the read.
- A read changes the answers by itself in two ways: it adds the bases the rules found to the kept bases, and it drops
  kept bases and comparisons that name refs no longer there (removed bases stay removed). Before it saves, it reads the
  file again and applies only those changes to what the file then holds, so an answer the user gave while the read ran
  survives. It never writes over a file from a newer version, nor over one that exists but cannot be read, such as a
  hand edit with a typo. Either reads as empty, and a malformed one is logged.
- The graph is rewritten whenever it changed, parents before children, and a file in which a parent does not come
  before its child is damaged and is not loaded. It is read in full again when it holds commits cut off by a shallow
  clone that is no longer shallow (`--unshallow`); a clone deepened but still shallow (`--deepen`) keeps its cut
  commits cached until the cache is discarded.

## Rationale

Not the folder: a repository checked out in three worktrees would be asked the same question three times and keep
three caches. Not the working tree or the repository's own `.git`: the answers are the user's, not the project's, must
not leak into a commit or a backup of the repository, and some trees are read-only. The shared git directory is the
one name every worktree agrees on.

## Consequences

### Positive

- The second opening of a repository is instant from the cache: in the acceptance runs 196,143 commits load and the
  whole model runs in about a quarter of a second, against about two seconds cold.
- An agent's new worktree starts with the answers the user already gave.

### Negative

- Moving or renaming the repository's directory gives it a new folder, and the answers and the cache are lost; the old
  folder is left behind.
- Nothing prunes the folders of repositories that no longer exist.
- Two Graphite processes on one repository can write the same files; each write replaces a file as a whole. An answer
  saved between a read's second look at the answers file and its write is still lost; that window is a few
  milliseconds, not the length of the read.
- Commits of deleted branches stay in the cached graph; nothing prunes them.

### Neutral

- The window's own view state stays in `.graphite` per folder.

## Alternatives Considered

- **In `.graphite` in the opened folder.** Lost: per folder, not per repository.
- **Inside the git directory.** Lost: it writes into the repository, which this feature must not, and is not writable
  everywhere.
- **A database for answers and facts together.** Lost: the answers are a small file the user may read and copy; the
  facts are a rebuildable cache. They have different lifetimes and do not belong in one store.

## Related

- [0663](0663-compare-each-branch-with-the-line-it-forked-from.md) and
  [0664](0664-find-bases-from-server-defaults-merged-lines-and-authors-never-from-names.md) produce the state.
- `docs/superpowers/specs/2026-11-03-git-window-lines-design.md`, section 3.1.
