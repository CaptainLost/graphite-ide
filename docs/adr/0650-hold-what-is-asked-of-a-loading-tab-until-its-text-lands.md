# 0650. Hold what is asked of a loading tab until its text lands

Status: proposed
Date: 2026-10-27

## Context

[0642](0642-run-explorer-file-operations-and-large-file-loads-on-a-worker-and-show-them-while-they-run.md)
reads files above 4 MiB on a worker. Their tab exists for up to a few seconds
without text, and other parts of Graphite ask things of it in that time:

- Search, References, Problems and go-to-definition select a hit in it.
- Session restore gives it a saved caret and top line.
- The user closes it, saves the session, or retries a failed read.
- The file changes on disk while it is read.

Before 0642, each of these met a tab that already had its text.

## Decision

A tab whose file is still loading takes every such request as soon as its text
lands. Until then, it holds the request without acting on it.

- **A caret or selection** asked for during the load is applied when the text
  lands. The last request wins. While the file loads, the session saves the
  pending position as the tab's position.
- **Close and Ctrl+W** work on any tab, including a loading or a failed one.
  Closing cancels the read.
- **Retry** after a failed read reads again without the large-file question,
  unless the file has grown past the question's size without the user having
  accepted that size.
- **A change on disk during the read** runs the usual file sync when the text
  lands. The tab never keeps text older than the file.
- **The read builds what the first draw needs.** It uses the tab width and the
  word wrap of the last drawn editor, so the first draw does no work in
  proportion to the file. A pane of a different width wraps again when it draws.

## Rationale

The open path already settles a large file in one step on the UI thread.
Applying what was asked at that same step keeps every caller unaware of the
load: a click on a hit selects the hit, whether the file opened at once or after
two seconds.

## Consequences

### Positive

- A hit clicked in a file between 4 and 16 MiB is selected exactly as before
  0642.
- A long read can be given up from the keyboard.

### Negative

- A request made during the load does nothing visible until the text lands.
- The editor hands its tab width and wrap geometry to the document set every
  frame, so that a read can use them.

### Neutral

- Reloading a large file after it changes on disk is still synchronous.

## Alternatives Considered

- **Refuse requests while a tab loads.** A hit clicked in a file between 4 and
  16 MiB would open the file and lose the selection it gets today.
- **Apply a request only after the next frame draws the text.** That adds a
  frame of state for every caller, for no visible difference.

## Related

- [0633](0633-show-every-wait-with-the-core-loader-and-say-nothing-is-there-only-after-an-answer-said-so.md),
  [0642](0642-run-explorer-file-operations-and-large-file-loads-on-a-worker-and-show-them-while-they-run.md)
