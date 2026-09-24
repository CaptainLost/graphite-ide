# 0520. Read what an attachment sent from its rendered field

Status: proposed
Date: 2026-09-24

## Context

The Requests tab lists what each API request sent. Transcripts store
attachments as Claude Code's own objects, and the text the model got had been
guessed from fields such as `text` or `addedLines`, so sizes were wrong and
bookkeeping that never reached the model (`hook_success`, `prompt_snapshot`,
`deferred_tools_record`) was listed as sent. The system prompt and tool
schemas of the first request were missing.

## Decision

Since Claude Code 2.1.260 every attachment the model sees carries `rendered`,
the exact text sent; the reader takes it from there. From 2.1.260 on an
attachment without `rendered` is treated as not sent, with one exception: a
`queued_command`, which is always reconstructed. An older record, or one with
no readable `version`, is reconstructed from its fields as before and marked
approximate. `prompt_snapshot` — written once before and once after the
first request — supplies that request's system prompt and full tool schemas;
the latest snapshot replaces the earlier one at the head of request #1.

## Rationale

`rendered` is Claude Code's own statement of what it sent, so reading it
removes a heuristic that drifted with every attachment type. The version
boundary was measured across all local transcripts (2.1.241–2.1.259 never
write `rendered`; from 2.1.260 every sent attachment does, except queued
commands: 2.1.261 wrote a queued prompt without `rendered` that the model then
answered, so it was sent all the same). A queued prompt is what the user typed,
and hiding one would hide the very message a request answered. Showing an old
transcript's reconstruction, marked, is more useful than hiding it.

## Consequences

### Positive

- Sizes rest on the real text; the first request's context adds up; the
  details can show the text itself.

### Negative

- The boundary is a version number that a future Claude Code could
  invalidate.
- An old transcript's sizes remain estimates.
- Tool-call inputs and all sent text are now held in memory for the session
  (a few MB at most).

### Neutral

- `prompt_snapshot` is Claude Code's own record, not something sent: it is read
  for the system prompt and tools it describes and is never listed as an item.
- Only the first request gets the snapshot's items. Every later request sends
  the system prompt and tools again, but they are not listed there: a request's
  items are what it added to the context, and those were already in it.
- A first request with no snapshot, as in every transcript older than 2.1.260,
  lists none of the system prompt or the tools that make up most of its
  context.

## Alternatives Considered

- **Keep the field-by-field heuristic for every version.** Simpler — one code
  path, no version check — but the heuristic had already drifted from what
  the model actually received for more than one attachment type, and every
  future attachment type would need its own guess.
- **Hide requests from before 2.1.260 instead of reconstructing them.**
  Removes the approximate figures entirely, but throws away every session
  recorded before that version for a view whose whole purpose is to show what
  was sent.

## Related

- ADR [0517](0517-read-claude-transcripts-through-a-lazy-index-with-cached-summaries.md)
  (the transcript reader and index this reading is part of)
