# 0651. Blame HEAD once per commit and lay it over the buffer with a line diff

Status: proposed
Date: 2026-10-25

## Context

The editor is to show, for each line, who last changed it and in which
commit, and to list the commits that touched one line on request. The
answer git gives is for a commit, not for the text on screen: a blame of
HEAD describes the file as committed, while the reader is typing into a
buffer that has moved on from it. Every line the reader has edited or
added has no author yet, and every line they have only shifted down by
inserting above it keeps the author it had.

Computing a blame is the expensive part. It walks history, and it has to
run on a worker thread through the repository handle that thread owns
(0492). The buffer, on the other hand, changes on every keystroke, so
whatever lays the blame over it has to be cheap enough to repeat each time
the reader pauses, and has to stay out of the way of the UI thread
(0491). `DocumentWindow` cannot reach `GitModel`, so the editor needs a
model it can ask, the way it already asks `ChangeMarkersModel` for change
markers (0524).

## Decision

`BlameModel` (`Graphite/src/Git/Blame/BlameModel.h`) is a per-document
model next to `ChangeMarkersModel`, built the same way:

- The blame of HEAD is computed once per file and kept until HEAD or the
  file's committed content may have moved. It is read through
  `BlamePort::BlameAtHead` on the shared job queue, which
  `RepositoryGitPort` implements with the calling thread's own repository
  handle. Only `RepositoryChangedEvent` and `Invalidate()`, which the
  application calls when this process has written to the repository
  itself, mark the model dirty under the same debounce `GitModel` uses.
  When it settles, the head blame is read again only for the paths on
  screen: those passed to `Update` since the previous frame's `OnUpdate`,
  which forgets every frame which paths were shown. Every other path is
  marked stale and read again on its next `Update`, so a tab left in the
  background is not blamed on each commit or fetch, nor all at once on the
  first change after a quiet spell. A path has at most one head read in
  flight: a request made while one is out marks the path to be read again
  once the first answer lands, and that answer is still kept, so changes
  that keep coming cannot starve it.
  `WorkspaceFilesChangedEvent` is deliberately not subscribed to: HEAD's
  blame does not depend on the working tree, so saving a file never
  re-reads it. `WorkspaceOpenedEvent` and `WorkspaceClosedEvent` clear the
  model outright.
- The head blame is laid over the buffer with a line diff from HEAD's text
  to the buffer's text, using `Core::DiffTexts`. A buffer line that maps to
  an unchanged HEAD line takes that line's blame; every other line is
  uncommitted. `BuildLineAuthors` is a pure function of the head blame, the
  hunks and the buffer's line count, so it is tested without a repository.
- The overlay runs on a worker thread, after the buffer has been quiet for
  0.1 s, the same quiet period the change markers use. A document seen for
  the first time does not wait. A buffer longer than
  `Core::MaximumDiffTextSize`, and a file git does not track or cannot
  blame as text, has no authors; the check comes before the buffer is
  copied.
- Every request takes a generation from one model-wide counter that is
  never reset, and an answer whose generation is no longer the one its path
  waits for is dropped. An overlay or a line history can be overtaken: its
  answer is dropped once a newer request for the same path, or a `Clear`,
  has been made. A head read is never overtaken by another for its path,
  since a request made while one is out waits for it as above, so its
  answer is dropped only after a `Clear` or once the path is closed.
  Clearing a path's authors, because its head is untracked or failed or its
  buffer is over the cap, retires the overlay generation too, so an overlay
  already in flight cannot put a table back.
- A line history is asked through the same model, per path, and not
  through `GitModel`'s single log slot. `RequestLineHistory` ignores a
  request equal to the one already held or in flight, `GetLineHistory`
  reports what has arrived, and `ForgetLineHistory` drops it so that a late
  delivery is ignored. The blame follows every parent, so a line that came
  in through a merge is attributed to the commit that wrote it on its
  branch; its line history starts at that commit and walks that commit's
  first parents, never the merge's.
- A path is tracked on its first `Update`, and `Retain` drops the state of
  every closed document. A hidden document is not passed to `Update`, so
  its buffer is neither copied nor diffed, and a repository change only
  marks its head blame stale; nothing is computed for it until it is shown
  again.

## Rationale

A blame is a property of a commit and a line diff is a property of a
buffer, so computing each at its own rate is what keeps typing cheap: the
slow libgit2 walk happens once per commit, the fast text diff happens once
per pause. Because the overlay is a pure function over two plain values it
can be tested headless, and because the model copies the shape of
`ChangeMarkersModel` it inherits its debounce, generation and delivery
rules, which are already understood and tested.

## Consequences

### Positive

- Editing never calls libgit2: a keystroke costs one line diff on a worker
  once the reader pauses.
- Lines the reader has not touched keep their author and commit while the
  lines around them move, and edited lines show as uncommitted at once.
- The blame is shared by every pause, and is read again only when the
  repository changes or this process writes to it, and then only for a
  document on screen; saving a file costs no blame.

### Negative

- Another model repeats the debounce and delivery machinery of
  `ChangeMarkersModel` and `GitModel`, so a change to their timing rules
  does not carry over by itself.
- A file is read from HEAD once more for blame, on top of the bases the
  change markers read for it.
- The overlay trusts a line diff, which can pair a rewritten line with the
  wrong HEAD line; such a line shows the author of the line it was matched
  to rather than uncommitted.

- A change to a tracked file's committed content that git does not report
  as a repository change is not noticed until the next one; the working
  tree alone never changes a blame of HEAD.
- A document brought back from the background shows the authors of the
  HEAD it was last blamed at until its stale head blame has been read
  again.

### Neutral

- An owner holding this model and the job queue behind its `JobSubmitter`
  must declare the model before the queue, so that the queue and its workers
  are torn down first.
- The previous authors stay available until fresh ones arrive, so drawing
  code must bounds-check against the buffer's current line count.

## Alternatives Considered

### Blame the buffer with git_blame_buffer

libgit2 can blame a buffer directly against a reference blame, which would
make the authors of edited lines come out of the library itself. Rejected:
the reference blame is bound to the repository handle of the thread that
built it, and 0492 forbids sharing a handle between threads, so each edit
would need the same worker or a fresh reference blame. It would also call
libgit2 again after every edit, where a line diff is enough.

### Blame on the UI thread

Running the blame when a document is drawn needs no model, port or
generation counters. Rejected: a blame walks history and can take seconds
on a large file, and 0491 keeps git work off the UI thread.

### Extend ChangeMarkersModel with the blame

The model already tracks the same paths with the same events and debounce.
Rejected: its bases are the index and HEAD texts, read together and diffed
into change markers, while the blame is a different query with a different
result and its own line history requests. Folding them together would give
one model two outputs that go stale for different reasons.

## Related

- [0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) -
  git data comes from Core, and freshness is Graphite's to manage.
- [0492](0492-give-each-worker-thread-its-own-repository-handle.md) - why a
  blame cannot be handed from one thread's handle to another.
- [0524](0524-hold-change-markers-in-a-model-of-their-own-apart-from-gitmodel.md)
  - the model this one mirrors.
- `Graphite/src/Git/Blame/LineAuthors.h` - the overlay function.
