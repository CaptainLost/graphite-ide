# 0517. Read Claude transcripts through a lazy index with cached summaries

Status: proposed
Date: 2026-09-23

## Context

Claude Code writes every session as an append-only JSONL file under
`~/.claude/projects/<project>/`. On the machine this was designed on there are
251 of them, 145 MB in total, and the largest single session is several
megabytes. The sessions panel needs a title, a span and a cost for every one of
them to draw its list, and the full content of only the one being read.

## Decision

A worker (`SessionIndex`) summarises each session — title, first prompt, span,
tokens per model — and the summaries are kept in `sessions-cache.json` next to
the executable, keyed by a fingerprint of the session file and its subagent
files (newest write time, total size, file count). A later start uses every
cached summary whose fingerprint still matches and summarises only the rest.
The full transcript is parsed only when a session is opened, and an open
session is read incrementally from the byte offset reached last time, because
the file is only ever appended to.

## Rationale

The list is the first thing drawn and must not wait for 145 MB of JSON. Nearly
every session is finished and never changes again, so a fingerprint check makes
the second start cost a directory walk. Reading from an offset is what makes a
live session cheap to follow: a turn appends kilobytes, not the whole file.

## Consequences

### Positive

- The list appears immediately from the cache and fills in while the scan runs.
- Following a live session costs what was appended, not the whole transcript.

### Negative

- A second copy of every summary lives on disk and has to be versioned; a
  format change raises `kSummaryCacheVersion` and discards the old cache.
- Full-text search across sessions is not served by this index; it needs a
  store of its own.

### Neutral

- The cache lives beside the executable rather than under a workspace's
  `.graphite/`, where the workspace analysis keeps its store (ADR 0513): the
  sessions it summarises belong to the machine, not to any one workspace.

## Alternatives Considered

- **SQLite with FTS5 from the start.** Gives search and aggregate queries, but
  adds a dependency and a schema to migrate before any stage needs them.
- **Parse everything into memory at start-up.** Seconds of parsing and hundreds
  of megabytes held on every start, for sessions nobody opens.

## Related

- ADR 0511 (the workspace analysis, which likewise runs on a worker ahead of
  the reader)
- ADR 0513 (the workspace analysis cache, kept on disk and discarded on any
  doubt, as this cache is when its version changes)
