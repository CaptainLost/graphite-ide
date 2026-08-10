# 0420. NL.2: State intent in comments

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

Code says what is done, not what is supposed to be done. Often intent can be
stated more clearly and more concisely than the implementation states it.

```cpp
void stable_sort(Sortable& c)
    // sort c in the order determined by <, keep equal elements (as defined by ==) in
    // their original relative order
{
    // ... quite a few lines of non-trivial code ...
}
```

The body is the algorithm; the comment is the promise. A reader who wants to
know whether they may call this function needs the promise, and reconstructing
it from the body would take far longer than reading two lines.

One warning is attached: if the comment and the code disagree, both are likely
to be wrong. A stale intent comment is not merely noise, it is evidence that the
author of the change did not understand what the function was for.

## Decision

Write comments that say what the code is supposed to achieve.

- Document a function by its purpose and its guarantees: what it does for the
  caller, what it promises about the result, and what it deliberately does not
  do.
- Put the intent where the reader meets the code — at the declaration for
  callers, and inside the body only where a step's purpose is not evident from
  the step.
- Record why an unobvious implementation was chosen when the obvious one was
  rejected for a reason the code cannot show.
- When a comment and the code disagree, treat both as suspect and settle what
  was meant before changing either.

## Rationale

Intent is the one thing that cannot be recovered by reading harder. An
implementation is fully present in the source, so a determined reader can always
extract it; the specification it was written against exists nowhere unless
someone writes it down. That asymmetry is why the intent comment survives the
cull that [0419](0419-nl-1-dont-say-in-comments-what-can-be-clearly-stated-in-code.md)
performs.

## Consequences

### Positive

- A caller can decide whether a function is the right one without reading its
  body.
- The comment becomes a specification the implementation can be checked against,
  which is what makes a divergence detectable at all.
- Reviewers get something to review against, rather than only judging whether
  the code does what it does.

### Negative

- An intent comment is a second statement of the contract, and it can rot; a
  confidently wrong one is worse than no comment, because it is believed.
- Writing intent honestly is slow, and it is hardest exactly where it matters
  most — the function whose purpose the author has not fully settled.
- The rule offers no boundary against over-documentation; taken enthusiastically
  it produces prose that [0421](0421-nl-3-keep-comments-crisp.md) then has to
  cut back.

### Neutral

- The rule does not say where intent must be stated. A precondition expressed as
  an assertion or a strongly typed parameter states it in code instead, which is
  better still.
- The example places the comment between the signature and the opening brace.
  That placement is incidental; above the declaration serves the same purpose
  and reaches callers reading a header.

## Related

- [0419](0419-nl-1-dont-say-in-comments-what-can-be-clearly-stated-in-code.md) —
  the same subject from the other side: remove what the code already says.
- [0421](0421-nl-3-keep-comments-crisp.md) — intent stated at length stops being
  read.
- [0009](0009-p-3-express-intent.md) — the philosophy rule this is the comment
  form of.
- [0024](0024-i-5-state-preconditions-if-any.md) and
  [0026](0026-i-7-state-postconditions.md) — preconditions and postconditions
  are the parts of intent with a mechanical form.
