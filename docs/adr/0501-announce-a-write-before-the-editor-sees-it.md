# 0501. Announce a write before the editor sees it

Status: proposed
Date: 2026-08-21

## Context

`DocumentWindows` already handles a file changing under the editor:
`FileSyncOutcome::SafeToReload` reloads a clean buffer silently, and a dirty
buffer raises `m_ReloadConflict`, which asks whether to reload and lose the
unsaved changes. That is right for a change made behind the user's back, which
is the only kind that could happen until now.

Discarding a file from the changes panel is not that kind. The sequence without
further work is: the panel asks for confirmation, naming the dirty buffers it
is about to overwrite; the user confirms; git rewrites the file; the watcher
fires; and `m_ReloadConflict` asks about the same file again, in wording
written for a surprise. The user is asked twice about one decision, and the
second question implies something went wrong.

## Decision

The panel tells the editor that a change it is about to see came from here:
`DocumentWindows::ExpectExternalWrite(paths)`. A path registered this way
reloads without asking.

**The bounds are the decision, not the suppression.** A lingering entry would
silence a genuine external change later, so an entry is:

- **registered only after the write reports success**, never before the request
  is sent, which narrows the window to the milliseconds between the write
  landing and the watcher noticing it;
- **one-shot** — the first sync check for that path consumes and removes it;
- **expiring after `kExpectedWriteSeconds = 2.0f`**, in case the event never
  arrives. Two seconds because `kReloadAfterSecondsAway` in the same file is
  already two, and a second competing time constant would be a coincidence
  rather than a decision;
- **cleared when the folder closes.**

**Matching is by whole absolutised path, case-folded.** A git status names a
path relative to the repository's working directory; an open document names an
absolute one. The panel absolutises the status path against the repository's
working directory before registering it, and both sides are normalised —
`lexically_normal` to collapse `..` without touching a filesystem that may
already have deleted the file, and `make_preferred` so git's forward slashes
and the query side's native separators compare byte for byte.

**The working directory comes from the repository, not from the folder the user
opened.** `git_repository_open_ext` searches upward, so opening a subfolder of
a repository leaves status paths relative to a directory above the opened one.
`GitModel` carries the repository's own working directory, fetched on the same
worker job as the status so that the two can never be known apart.

## Rationale

**Why the obvious matching rules are wrong.** Two were written and withdrawn
under review, and they are recorded because both look correct.

Matching on filename plus a path suffix fails twice over. A path with no
directory component — every file at the repository root — degenerates into
"match this filename anywhere in the tree", so an expectation registered for
`a.txt` claims a genuine external change to `src/a.txt` and reloads a **dirty**
buffer with no prompt at all, destroying unsaved work silently. And in practice
it never fired even for the case it was written for, because libgit2 emits
forward slashes while document paths carry native separators, and the
comparison was a raw string compare.

**The failure directions are not symmetric, and that asymmetry is the design
rule.** A false positive here destroys unsaved work with no prompt and no
undo. A false negative merely asks the user a question they have already
answered. Every choice in this matching must therefore fail toward asking:
prefer a comparison that is too strict, register too late rather than too
early, expire too soon rather than too late.

Joining a status path onto the opened folder rather than the repository's
working directory made the panel describe and act against two different
directories — it would suppress for a path that does not exist and act on one
it never announced. Fetching the working directory on the same job as the
status is what makes the pairing impossible to get wrong: there is no moment
when a status is held without the directory it is relative to.

## Consequences

### Positive

- One confirmation for one decision. The panel's dialog is the place the user
  is asked, and it is the place that can name what will be lost, which the
  editor's generic reload prompt cannot.
- The suppression cannot outlive the write that justified it: it is registered
  after success, spent on first use, and gone within two seconds regardless.
- Comparing whole absolutised paths means a file at the repository root is no
  more permissive than one nested five directories deep.

### Negative

- The editor now has a piece of state that exists only to change how it answers
  a question, and that state is written by something outside it. A future
  caller registering a path it has not actually written would make the editor
  silently reload over unsaved work, and nothing in the type prevents that.
- Two seconds is a guess about how long a watcher takes to notice. Too short
  and the user is asked anyway — the benign failure; too long and the window in
  which a genuine external change is swallowed grows. The constant is shared
  with `kReloadAfterSecondsAway` so that there is one number to reconsider
  rather than two.
- The suppression is per path, and it does not scale unchanged to an operation
  that rewrites many files at once. Branch operations will have to revisit it.

### Neutral

- `GitModel` gained the repository's working directory as state it did not
  previously carry. Anything else that needs to turn a status path into an
  absolute one now has one place to ask.

## Alternatives Considered

### Suppress by widening the reload prompt's wording instead

Ask the same question, but phrased to acknowledge that the change may have come
from the panel. No new state anywhere, and no risk of silencing a real external
change. Rejected because the user is still asked twice about one action, and a
prompt that hedges about where a change came from is less informative than the
one it replaces, not more.

### Have the panel reload the affected documents itself after a discard

Direct, and it needs no expectation set at all: the panel knows exactly which
paths it wrote. Rejected because it puts the editor's reload rules — dirty
buffers, encodings, cursor restoration — into a git panel, and because the
watcher would still fire afterwards and raise the very prompt this is meant to
avoid.

### Register the expectation before sending the write

Removes the race entirely: the entry is in place before the watcher could
possibly fire. Rejected because a write that fails, or that never runs because
the folder closed, would leave an entry armed against a file nothing touched —
exactly the lingering entry the bounds exist to prevent, and on the destructive
side of the asymmetry.

### Match on filename plus path suffix

Tempting because it sidesteps the relative-versus-absolute problem without
knowing the repository's working directory at all, and it reads as safely
narrow. Rejected for the two failures described in the Rationale: it degenerates
to a bare filename match for every file at the repository root, which is the
false positive that silently destroys unsaved work, and separator mismatch
meant it did not fire even when it should have.

## Related

- [0499](0499-let-core-repository-write-not-only-read.md) — the discard and
  delete calls this suppression covers.
- [0500](0500-serialise-git-writes-in-one-queue.md) — the queue whose success
  is what registers an expectation.
- `docs/superpowers/specs/2026-08-21-git-writes-design.md` — the design this
  record was extracted from.
