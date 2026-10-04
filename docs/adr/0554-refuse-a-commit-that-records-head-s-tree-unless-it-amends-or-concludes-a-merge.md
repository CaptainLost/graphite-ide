# 0554. Refuse a commit that records HEAD's tree unless it amends or concludes a merge

Status: proposed
Date: 2026-10-04

## Context

The commit button was enabled whenever a message was typed, and the engine
wrote whatever it was asked to. With nothing staged, a commit recorded HEAD's
own tree again: a commit that changes nothing, which git itself refuses unless
asked to allow it. Two cases legitimately record an unchanged tree: an amend
that only rewords HEAD's message, and a merge whose result happens to equal
HEAD's tree but still joins a second parent.

## Decision

- The Changes panel disables the Commit button while nothing is staged, unless
  the commit amends or a merge is in progress. The button's tooltip says why it
  is disabled - a write in progress, nothing staged, or no message - and the
  same function, `WhyCommitIsDisabled`, decides both whether the button is
  enabled and what the tooltip says.
- `Repository::Commit` refuses, with "nothing to commit", a commit whose tree
  equals HEAD's, unless it amends or the repository is in the merge state. This
  is the backstop for a caller that did not check first. The first commit in a
  fresh repository has no HEAD to compare with and is never refused by it.

## Rationale

An empty commit is almost always a mistake, and the panel is the place to stop
it, where the reason can be shown next to the button. The engine check exists
because the panel is not the only caller, and a rule kept only in the UI is
lost by the next one. Exempting amend and merge keeps the two cases where an
unchanged tree is the point.

## Consequences

### Positive

- No commit that changes nothing is written by accident.
- A disabled button tells the reader what to do next instead of just being grey.

### Negative

- A deliberately empty commit, sometimes used to trigger a build, cannot be
  made from Graphite; it needs the command line.
- A cherry-pick or revert whose result equals HEAD's tree is refused the same
  way, where git would stop with its own message.

### Neutral

- The panel decides "nothing staged" from the status it already holds; the
  engine compares trees. They agree except in rare races, which the engine
  resolves.

## Alternatives Considered

- **Only the engine check.** Lost because the reader would learn of the refusal
  only after pressing the button, as an error.
- **Only the panel check.** Lost because a future caller of `Commit` would get
  no protection.
- **Ask "commit anyway?".** Lost because the empty commit is almost never what
  was meant, and the question would teach readers to click through it.

## Related

- `Graphite/src/Git/Changes/CommitBlocker.h`, `Core/src/Core/Git/RepositoryWrite.cpp`
