# 0643. Preview only files that open without asking

Status: proposed
Date: 2026-10-26

## Context

Selecting a hit in Find in Files or in References read the whole file on the UI
thread to preview it. Each line of a References group read its file to show the
line. Opening a file asks first above 16 MiB. A preview had no limit below
512 MiB: reading 100 MiB took 0.27 s, and a 300 MiB file took about 0.8 s, on
every selection.

## Decision

A preview reads a file only if the file is within the size that opens without
the large-file question. The size comes from the existing probe, without reading
the file.

- **Above that size, the preview pane** says "File too large to preview" with
  the size, and offers Open. Open goes through the normal path, so it asks the
  question.
- **Above that size, a References hit** shows its location without its line
  text.

## Rationale

The open path already decided which files are too big to read without asking. A
preview is a cheaper glance at the same file and should not read more than
opening would.

## Consequences

### Positive

- Browsing hits in large files no longer stalls on each selection.

### Negative

- Hits in large files show no context until the file is opened.

### Neutral

- The limit moves with the open threshold if that ever changes.

## Alternatives Considered

- **Load previews in a job.** That removes the stall but still reads hundreds of
  megabytes per selection, and keeps them in memory, to show a few lines.
- **Read only the lines around the hit.** The preview pane shows the whole file,
  scrollable. A partial reader is a second text model for an edge case.

## Related

- [0642](0642-run-explorer-file-operations-and-large-file-loads-on-a-worker-and-show-them-while-they-run.md)
