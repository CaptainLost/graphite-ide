# 0595. Keep an unreadable state file as .bak before writing over it

Status: proposed
Date: 2026-10-16

## Context

Graphite keeps its state in JSON files the user can open and edit by hand:
`graphite.json`, a folder's `.graphite/workspace.json` and `reviews.json`, the
trust list `trusted-folders.json`, colour schemes under `themes/`, the install
record of each language server, and the sessions summary cache.

A survey of how each is loaded and saved found that they disagreed about a file
that does not parse. The settings, the workspace file and the reviews copied
it to `.bak` before writing over it, and refused the write when the copy
failed ([0583](0583-keep-review-marks-in-their-own-file-beside-the-window-state.md)
already says so for the reviews). The trust list and the colour schemes wrote
over it without a copy: one slip in a hand edit, followed by trusting a folder
or saving a colour, lost every folder the list named or every colour the scheme
held.

The same survey found the stored version read five different ways, each with
its own handling of a field that is missing or of the wrong type.

## Decision

- Before writing over a state file that does not parse, copy it to
  `<file>.bak`. If the copy fails, do not write; log why.
- A cache that is rebuilt from other files, such as the sessions summary cache,
  is exempt: it is written over.
- Read a state file's version through `StoredVersionOf`, which answers
  Missing, NotANumber, Older, Current or Newer. What each answer means for
  reading and writing stays the file's own policy.

## Rationale

The user's hand edit is the one thing a write can destroy that nothing can
rebuild. A copy costs one file beside the original and keeps the cost of a
broken edit to retyping the broken part, whichever file it was in.

## Consequences

### Positive

- A broken hand edit of any state file survives the next save as `.bak`.
- A new state file has one rule to follow instead of five examples to choose
  from.

### Negative

- A file that cannot be copied stops being saved for the session: changes made
  meanwhile, such as a newly trusted folder, are lost when Graphite closes.
- `.bak` files are left for the user to delete; nothing cleans them up.

### Neutral

- Saving a colour scheme reads the file on disk first, to learn whether it
  parses.

## Alternatives Considered

- **Refuse to write and tell the user to fix the file.** Nothing is lost on
  disk, but trusting a folder or saving a colour silently does nothing until
  the user notices the log, and a broken file blocks every later change.
- **Write over it, as the trust list and the schemes did.** Simplest, and it
  loses the hand edit outright.

## Related

- [0583](0583-keep-review-marks-in-their-own-file-beside-the-window-state.md)
  applies this to the reviews first.
