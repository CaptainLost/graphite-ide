# 0497. Name a file in a history filter by name or pattern, not by path

Status: proposed
Date: 2026-08-21

## Context

The history filter narrows by file through `LogRequest::File`, which the walk
matched by comparing whole paths for equality. That is what `git log -- <path>`
does, and it is right for the two callers that existed: a file's history and a
line range's history, both of which are opened on a file already in hand and
pass its full path.

It is wrong for a reader typing into a box. What they know is the file —
`Application.cpp` — and not the directories above it; the tree is often what
they opened the history to be told. Typing the name matched nothing, and the
box said nothing about why. Git has the same rule and hands the reader `**` to
work around it; the box handed them nothing.

A pattern also answers a question the exact form cannot: "which commits touched
any .cpp", which is a shape rather than a file.

## Decision

Match `LogRequest::File` as a pattern. A pattern with no separator is matched
against the file name alone, so `Application.cpp` and `*.cpp` name a file
wherever it sits. A pattern with a separator is matched against the whole path,
which keeps the existing callers exactly as they were.

Match whole, never as a substring: `Layer.cpp` does not name
`GraphiteLayer.cpp`. `*Layer.cpp` says that on purpose.

Neither `*` nor `?` crosses a `/`, so a pattern with separators says how deep it
means.

Follow a rename by substituting the renamed file's old path for the pattern
only when the pattern named exactly one file in that commit and carries no
wildcard. A pattern with a wildcard names a set, and a set has no single
identity for a rename to carry.

Share one matcher: `Core::GlobMatches` in `Core/Base/Glob.h`, which the search
panel's `MaskAccepts` now calls instead of keeping its own copy.

## Rationale

The two forms are told apart by something the reader already types on purpose
rather than by a mode they have to set. A separator means they are being
specific about where; its absence means they are naming a file and letting the
tree be found. Neither reading needs explaining in the box.

Keeping the whole-path form exact is what makes this safe to do to a field the
file and line histories already depend on: those pass a path with separators,
so their behaviour is unchanged, and the rename-substitution rule is written so
that only the single-file case can narrow the walk the way it always did.

## Consequences

### Positive

- The name a reader knows is the name that works, and `*.cpp` means what it
  looks like.
- The search panel and the history filter now agree about what `*` and `?` mean,
  because they run the same matcher.
- The file and line histories are untouched: a path with separators still
  matches exactly one file, renames included.

### Negative

- A bare name can name more files than the reader meant — two `Main.cpp` in
  different directories give one mixed history. The full-path form is the way
  back, and the reader has to notice they need it.
- Matching is now a scan per file per commit rather than a comparison, on a
  query that already diffs every commit it passes. Small against the diff, but
  not nothing.
- A rename is followed for a bare name and not for a wildcard, which is a rule
  the box does not state. It is the only defensible pair of answers, but a
  reader comparing the two histories could be surprised.

### Neutral

- `**` is not supported and is not needed: a bare name already matches at any
  depth, which is what `**/name` was being written for.

## Alternatives Considered

- **Match any tail of the path on a directory boundary.** `App/Main.cpp` and
  `Main.cpp` would both work with one rule and no separator test. Rejected
  because it changes what a full path means: `src/App/Main.cpp` would also match
  `Graphite/src/App/Main.cpp`, which quietly widens the file and line histories
  that pass full paths.
- **One box for everything, text and file alike.** Simplest to use. Rejected
  because it makes every search diff every commit walked, and because one text
  cannot mean "this author in this file" — the two boxes are what make that
  question askable.
- **A second field for the pattern, leaving `File` exact.** No risk to the
  existing callers. Rejected as two ways to say "narrow by file", differing in a
  way no caller would remember.

## Related

- [0496](0496-filter-the-history-in-the-walk-not-in-the-page.md) — the filter
  this field belongs to, and why it narrows the walk rather than the page.
