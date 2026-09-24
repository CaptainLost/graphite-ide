# 0521. Show what a request sent by size, not by guessed tokens

Status: proposed
Date: 2026-09-24

## Context

A transcript records tokens only per request: its usage block gives the
context, the cache layers and the output, never a count for any one item the
request carried. The Requests tab used to split a request's context delta
across its sent items by the share of the total bytes each one held, and
showed the result as an estimated token figure beside every item. The
tokenizer Claude actually uses is not available locally, and no other
tokenizer is a safe stand-in: general-purpose tokenizers miscount English and
code text by 15% or more against Claude's own, and by more still for other
languages. The byte split compounded that guess with a second one of its
own — an image is billed by the pixels it decodes to, not the bytes of its
base64, so the split routinely put an image's estimate far from its real
cost — and it could give no figure at all for a session's first request
before ADR 0520 taught the reader to read the prompt snapshot's system
prompt and tool schemas from the transcript.

## Decision

An item in the Requests tab shows only its size in bytes, with "≈ " in front
when the text itself is a reconstruction rather than the transcript's own
record of what was sent. No item carries a token figure. Token figures
appear only where the transcript actually records them: a request's context,
its context delta, its cache layers and its output — the numbers the usage
block gives, not numbers derived from them.

## Rationale

A reader comparing two items' sizes can already tell which one likely cost
more of a request's context; a number dressed up as a token count invites
trusting it as one, and the split's arithmetic being exact — the estimates
always summed to the delta — made it look more authoritative than the guess
underneath it ever was. Removing the guess is more honest than sharpening it:
every tokenizer available locally is the wrong one.

## Consequences

### Positive

- No figure in the Requests tab claims a precision the data does not have;
  every number shown is either a plain byte count or read straight from the
  transcript's usage block.
- One less thing to keep correct as attachment types and their sizes change:
  the estimate had its own rounding and its own edge cases (a shrunk
  context, a first request with no snapshot) that a byte count does not.

### Negative

- A reader can no longer see, at the item, which one cost the tokens in a
  request's context delta — only which one is biggest in bytes, which is a
  weaker signal for an image or for cached content.
- Exact per-item counts remain possible only through Anthropic's
  `count_tokens` API, which sends the text to Anthropic and needs API
  credentials the session viewer does not otherwise require; that trade was
  not one to make for a debugging view.

### Neutral

- The "Tools" line the same tab shows is unaffected by this decision: it
  already read from the transcript's own counts, not a guess, and is
  changed separately to say which of those counts came with a full schema.

## Alternatives Considered

- **Keep the byte split, but weight images by an estimate of their decoded
  pixels instead of their bytes.** Narrows one source of error but not the
  other: general tokenizers still miscount text, and a second heuristic on
  top of the first is still two guesses wearing one number.
- **Call Anthropic's `count_tokens` API for an exact count.** Deferred
  rather than dropped: it would give a real number, but it sends the
  session's text to Anthropic on every view of the tab and needs API
  credentials this viewer does not otherwise ask for, which is a cost this
  change was not asked to take on.
- **Use a general-purpose tokenizer (e.g. tiktoken) as a local stand-in.**
  Rejected: measured against Claude's own tokenizer it is wrong by 15–20%
  or more, which is worse than showing no number at all for a reader who
  would otherwise trust the figure.

## Related

- ADR [0520](0520-read-what-an-attachment-sent-from-its-rendered-field.md)
  (the transcript reading this change builds on: without its `rendered`
  text and its prompt-snapshot reading, there would be no real sizes to
  show in place of the estimate)
