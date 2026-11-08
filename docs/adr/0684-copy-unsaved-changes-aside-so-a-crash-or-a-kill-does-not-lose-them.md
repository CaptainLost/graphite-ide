# 0684. Copy unsaved changes aside so a crash or a kill does not lose them

Status: proposed
Date: 2026-11-08

## Context

ADR 0522 restores a folder's tabs after a crash but left unsaved buffer
contents out. A hands-on test pass on 2026-11-08 edited a file, waited a
minute, killed the process and started it again: the edits were gone, with
no copy anywhere. A normal exit asks about every unsaved change, so the loss
is only ever on an exit nobody was asked about — a crash, a kill from the
task manager, the session ending — and those are exactly the exits a reader
cannot prepare for.

Several launches can run at once against the same user data: two windows,
or a reader's own Graphite beside a test instance. A launch must never take
the unsaved text of another that is still running.

## Decision

Keep a copy of every open document's unsaved text in
`<local user data>/unsaved/<launch>/`, one directory per launch, named at
random. The directory holds a `lock` file the launch keeps open with no
sharing and delete-on-close (`Core::FileLock`), and one `<n>.copy` file per
document: the line `graphite unsaved copy 1`, the document's absolute path
in UTF-8, a newline, then the buffer's text as the editor holds it (UTF-8,
LF line endings), so reading it back needs no knowledge of the file's own
encoding.

`UnsavedCopies` is shown the document store every frame. A modified
document is copied once its text has gone unchanged for one second, or after
five seconds while it keeps changing, so continuous typing is never left
uncopied for longer than that. A copy is deleted as soon as its document is
saved, undone back to the file, or closed. A document over 64 MiB is not
copied. The writes and deletions run on one worker of their own, in the
order asked; the buffer is materialised on the UI thread, the file written
on the worker.

At start, before the startup folder is opened, every launch directory whose
lock can be taken belongs to a launch that is no longer running. Its copies
are claimed: each copy's text is put into its document — loaded into the
store first, so the folder's restored tab shares it — as one
`ReplaceContent`, which Undo takes back to the file on disk. A tab is opened
for each, without the large-file question, and a toast names the files that
came back. A claimed copy is deleted only once this launch has copied its
document again, or the document is no longer modified, so a crash in the
first second after the restart loses nothing either. A copy whose file
cannot be read, gone or read-only, stays where it is, for the next launch,
and an error toast names the copy file.

On exit, the copies are discarded when the close question was answered
(Save All or Discard All) or nothing was modified; on any other exit every
modified document is copied at once and every copy kept.

This amends ADR 0522's "unsaved buffer contents are not restored".

## Rationale

Copying beside the user data rather than into the folder keeps the folder's
own `.graphite` out of it: a copy is about this machine and this launch, and
must not be committed by accident. A directory per launch, with a lock that
a dying process lets go of, answers "is that launch still running?" without
process ids, which are reused. Putting the text back as one undoable change
rather than as the document's saved state keeps the file on disk the
reference: the tab shows modified, Save writes the restored text, Undo
returns to the disk.

## Consequences

### Positive

- A crash, a kill or a power cut after the copy landed loses at most the
  last second or five of typing.
- Two launches never take each other's copies.

### Negative

- Every modified document is written out again after each pause in typing;
  for a document near the 64 MiB limit that is a 64 MiB write every few
  seconds while it is edited.
- A copy whose file is gone comes back as an error at every launch until the
  reader deletes it by hand; nothing in the application lets them pick it up.
- A file changed on disk while Graphite was down gets the copy put over the
  new content; Undo goes back to the new content, not to what was there when
  the copy was taken.

### Neutral

- The copy is the editor's text, not the file's bytes: a file in UTF-16 or
  with CRLF comes back as the same document and is saved in its own encoding
  and line endings as before.

## Alternatives Considered

### Copies in the folder's `.graphite`

Rejected: it writes into the reader's project, where git sees it, and two
launches with the same folder open would share one directory.

### A copy beside each file (`name.ext~`)

Rejected for the same reason, more so: the copies land among the sources.

### Write copies on the UI thread

Rejected: a multi-megabyte document would stall a frame after every pause
in typing.

## Related

- ADR 0522 — the session state this extends.
- `Graphite/src/Persistence/UnsavedCopies.h`/`.cpp` — the copies, the claim
  and `PutBack`.
- `Graphite/src/App/GraphiteLayer.cpp` — `OnAttach` claims and puts back,
  `OnUpdate` observes, `OnDetach` discards or keeps.
