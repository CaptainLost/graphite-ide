# 0502. Read a working tree file's text from the index and the disk

Status: proposed
Date: 2026-08-22

## Context

A diff pane draws two things. The hunks come from the diff itself. Everything
around them — the syntax colouring, and the whole-file view that shows changed
lines in the context of the rest of the file — comes from the file's entire
text, both sides of it.

`Core::Repository::ReadBlob` is how that text was fetched, and it takes an
`Oid`. Every side it can answer for is a side that lives in a commit. That was
enough while a diff pane could only show a commit against its parent.

The changes panel broke the assumption. A working-tree diff compares the index
against the disk, or HEAD against the index, and a commit is the one thing
neither of its sides is. So a working-tree tab shipped with hunks and no text
behind them: no colouring, and no whole-file view, both from the same missing
piece. [ADR 0500](0500-serialise-git-writes-in-one-queue.md) recorded that as a
known gap.

Two properties of that text make it unlike a commit's, and both matter here. It
changes on every keystroke, so nothing about it can be cached the way a commit's
text is. And it is read from the working tree rather than from the object
database, so its cost is paid against the filesystem, repeatedly, for as long as
the tab stays open.

## Decision

`Core::Repository` learns to read the two sides a commit cannot supply:
`ReadIndexBlob` for a file as it is staged, and `ReadWorkingTreeFile` for a file
as it is on disk. `HeadCommit` names the commit the third side lives in, so the
port can reach it without walking a log to find it.

Which two sources a target compares is `Graphite::SidesFor`, a function of the
target alone: unstaged compares the index against the disk, staged compares HEAD
against the index. `RepositoryGitPort::ReadWorkingTreeTexts` reads whichever two
it names.

`GitModel` fetches that text **on the same worker job as the working-tree
diff**, from the paths the diff itself reports, and delivers both together into
one slot it replaces on every refresh.

`FileTextsBinding::UpdateFromWorkingTree` re-lexes only when the text has
actually changed, decided by `TokensStillGood` comparing both sides' content and
presence.

`Core::MaximumDiffTextSize` bounds both new reads at two megabytes. Over it, the
read is refused and the pane draws its hunks with no text behind them.

## Rationale

**One job, not two.** The text and the hunks drawn over it describe a file the
reader may be typing into. Fetched separately they would describe two different
moments of it, and the discrepancy would be worst exactly when the file is being
edited — which is when a working-tree diff is looked at. Taking the paths from
the diff rather than from the request is the same argument applied to renames:
the request names one path, and a renamed file has two.

**A slot, not a cache.** A commit's text is immutable, which is what makes it
worth keying and keeping. This text is different every time the file is touched,
so a cache of it would be a cache of answers that are already wrong. The slot
mirrors the working-tree diff beside it, for the same reason and with the same
lifetime.

**Compare the bytes, not the pointer.** `Update` can tell stale tokens from
fresh ones by pointer identity, because a commit's text never changes under a
given pointer. Here the model hands back a new object on every refresh whether
or not the file moved, so the pointer says nothing. Comparing the text is what
stops a file being re-lexed continuously while it sits untouched — the mistake
`LexFileText`'s own header warns against.

**A ceiling, because this read repeats.** `MaximumFileSize` bounds a document
the user asked to open: paid once, for a file they named. This cost is paid
again whenever the working tree changes, and is followed by lexing. Two
megabytes is past any file that is read as source, and the failure it produces —
a diff with no colouring — is the same one the reader would have got before this
record.

## Consequences

### Positive

- A working-tree diff draws the same as a commit's: coloured, with a whole-file
  view. The two panes stop being different kinds of thing.
- `SidesFor` and `TokensStillGood` are the two decisions this adds, and both are
  functions of their arguments — checkable with no repository, no thread and no
  file.
- A renamed file in the working tree gets each side lexed under its own path, so
  a rename that changes the extension really is two languages.

### Negative

- Two more reads on `Core::Repository`, and a third way for a caller to ask for
  a file's bytes. Which of the three is right now depends on knowing which side
  of which comparison is wanted.
- The working tree's text is read from disk on every refresh of an open tab.
  Bounded by the size ceiling and by the one-in-flight guard on the diff it
  travels with, but it is filesystem work that a commit's diff never does.
- The size ceiling is a number, and a number is a guess. A generated file just
  under it is read and lexed repeatedly; one just over it silently loses its
  colouring, and nothing on screen says which side of the line it fell.

### Neutral

- `ReadIndexBlob` reports `NotFound` for a conflicted path, which has no single
  staged content but one per side of the merge. Picking one and calling it "the"
  staged version would be a guess presented as a fact.

## Alternatives Considered

### Widen `ReadFileTexts` to take a sentinel revision meaning "the working tree"

One method, one call site, no new reads. Rejected because the sentinel would
have to be a value an `Oid` can hold, and every caller would then have to know
which values are commits and which are instructions. The type would stop meaning
what it says.

### Fetch the text on its own request, after the diff arrives

Simpler in the model — the text request could carry the file's two paths, taken
from the diff already on screen, and skip the "one file" special case. Rejected
because it puts the text a refresh behind the hunks drawn over it, and the file
this is used on is the one being edited.

### Cache the working tree's text by path, like a commit's

Reuses the existing cache and its eviction. Rejected because every entry would
be stale the moment the file was touched, so the cache would answer with the
wrong text or be invalidated on every change — which is a slot with extra steps.

## Related

- [0500](0500-serialise-git-writes-in-one-queue.md) — recorded this gap as a
  known cost of the write feature; this record closes it.
- [0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) — the
  boundary the new reads sit on: the reads are Core, deciding when to re-read
  and when to re-lex is Graphite.
