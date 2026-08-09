# 0051. F.15: Prefer simple and conventional ways of passing information

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

The opening rule of the F.call subsection, which governs parameter passing. It
introduces F.16 through F.21 and sets the default: use the ordinary form unless
a measurement says otherwise.

## Decision

Adopt F.15 as written. Transcribed in full:

> **Reason.** Using "unusual and clever" techniques causes surprises, slows
> understanding by other programmers, and encourages bugs. If you really feel
> the need for an optimization beyond the common techniques, measure to ensure
> that it really is an improvement, and document/comment because the improvement
> might not be portable.

The following tables summarize the advice in the following Guidelines, F.16-21.

- Normal parameter passing table
- Advanced parameter passing table

Use the advanced techniques only after demonstrating need, and document that
need in a comment.

For passing sequences of characters see String.

**Exception.** To express shared ownership using `shared_ptr` types, rather than
following guidelines F.16-21, follow R.34, R.35, and R.36.

**Note on the transcription.** The two summary tables are images in the source
document, not text, so they cannot be carried across here. Their content is the
same advice spelled out in F.16 through F.21, each of which is transcribed in
its own record.

## Rationale

Adopted as written, and it is the rule the rest of F.call hangs from. Its
practical force here is the second half: an advanced passing technique needs a
measurement and a comment, and this project has no benchmark — so in effect,
`&&` parameters and perfect forwarding need a written justification or they do
not belong.

That is a comfortable position for this codebase, which has almost no
performance-motivated parameter passing. Values are passed by `const&` or by
value, and `std::move` appears where ownership genuinely transfers.

The `shared_ptr` Exception is inert: shared ownership is not used anywhere, per
[0046](0046-f-7-for-general-use-take-t-or-t-arguments-rather-than-smart-pointers.md).

## Consequences

### Positive

- Sets the default to the conventional form, which is the one a reader can check
  at a glance.
- Makes "clever" passing a thing that must be argued for rather than a matter of
  style.

### Negative

- The rule's own summary tables are the most useful part and are not
  transcribable, so this record points at F.16-F.21 rather than answering the
  question itself.
- "Measure" is again advice this project cannot currently follow, so the bar for
  an advanced technique is effectively "never" rather than "when justified".

### Neutral

- String passing is deferred to a separate section of the guidelines, not yet
  transcribed.

## Related

- [0052](0052-f-16-for-in-parameters-pass-cheaply-copied-types-by-value-and-others-by-reference-to-const.md)
  — the next rule, and the first of the six this one introduces.
- [0050](0050-f-11-use-an-unnamed-lambda-if-you-need-a-simple-function-object-in-one-place-only.md)
  — the last rule of the preceding subsection.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — P.9, and the same missing
  measurement.
