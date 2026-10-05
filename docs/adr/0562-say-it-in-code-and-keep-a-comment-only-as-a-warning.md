# 0562. Say it in code and keep a comment only as a warning

Status: accepted
Date: 2026-10-05

## Context

[0479](0479-settle-one-house-style-for-naming-formatting-and-layout.md) removed
`///` documentation and kept plain comments that record "a non-obvious
invariant, a platform quirk or a trap", and
[0420](0420-nl-2-state-intent-in-comments.md) asked for intent to be stated in
comments. The code took both to heart: about sixteen thousand comment lines in
the sources and three thousand in the tests, many of them excellent. But the
survey behind [0558](0558-write-code-a-junior-engineer-can-read-like-prose.md)
also found about a hundred comments that had drifted from their code —
describing an earlier design, pointing at line numbers that had moved, citing
plans and specs a reader cannot open ("sub-project D", "Task 3", "Rule N") — and
one that contradicts the code and its tests. And it found that most of what the
good comments said could have been said by the code: a condition explained in a
comment could have been a named boolean, an off-by-one explained in a paragraph
could have been a function whose name states the rule.

A comment is not compiled, not tested and not updated by a rename. Code that
needs one to be understood is code that has not finished saying what it means.

## Decision

Move what a comment says into the code, then delete the comment.

The new home is one of:

- **a name** — `const bool selectionEndsAtStartOfLastLine = ...` in place of a
  comment explaining the comparison;
- **a function** — `ClampToRowIndex(mouseY - originY)` in place of a paragraph
  about negative floats cast to `std::size_t`;
- **a named constant** — `AlternateBufferMode` in place of `1049` and a comment;
- **an assertion** — a precondition stated as `CORE_ASSERT`, not as prose;
- **a test** — whose name states, as a sentence, the behaviour the comment
  described.

A comment survives only as a **warning**: a quirk of an external API or of the
platform, or the reason the obvious simpler version is wrong — the sentence that
stops the next reader from "fixing" correct code. It is one or two plain
sentences, written once at the definition that needs it, not repeated at each
call site:

```cpp
// CalcTextSize rounds each run up, so runs placed by its widths drift from the text.
float MeasureRunUnrounded(const char* begin, const char* end)
```

The reason given when a `Result` is deliberately discarded, which
[0537](0537-mark-result-nodiscard-on-the-type-not-on-functions.md) requires, is
such a warning and stays.

**Never in a comment:** what the code does; the code's history ("used to", "as
before", "now also"); line numbers or positions ("at 512", "the Tick below");
references to specs, plans, tasks, numbered rules or anything else a reader
cannot open from the repository.

**A comment is deleted only once its information has a home.** If the code
cannot carry it, it stays, as a warning.

## Rationale

Information in code is checked by the compiler or the suite and changes when the
code changes; information in a comment is checked by nothing and drifts, as a
hundred of them already had. Asking where each comment's content can live in the
code is also the most productive question a readability pass can ask: it is what
finds the missing name, the missing function and the missing test. The warning
exception exists because some facts are about the world outside the code — an
API's rounding, a platform's ordering — and no name inside the code can state
them.

## Consequences

### Positive

- What the code says about itself cannot go stale without breaking a build or a
  test.
- A surviving comment is rare enough to be read, and is always a warning worth
  heeding.
- Converting comments is what drives better names and smaller functions.

### Negative

- Knowledge that cannot be expressed in code and is judged not to be a warning
  is lost. [0479](0479-settle-one-house-style-for-naming-formatting-and-layout.md)'s
  deletion of documentation lost eight such facts that later had to be restored;
  the rule that a comment goes only once its content has a home is the guard,
  and it holds only as well as review enforces it.
- Names grow longer and helper functions more numerous to carry what comments
  used to.
- Reasoning about why a design is as it is moves out of the code entirely, into
  the records in this directory.

### Neutral

- [0419](0419-nl-1-dont-say-in-comments-what-can-be-clearly-stated-in-code.md)
  and [0421](0421-nl-3-keep-comments-crisp.md) stand; this record takes them to
  their conclusion.

## Related

- [0558](0558-write-code-a-junior-engineer-can-read-like-prose.md) — the
  principle this applies.
- Supersedes [0420](0420-nl-2-state-intent-in-comments.md).
- Amends [0479](0479-settle-one-house-style-for-naming-formatting-and-layout.md):
  an invariant now goes into code; a platform quirk or a trap stays as a warning.
- [0419](0419-nl-1-dont-say-in-comments-what-can-be-clearly-stated-in-code.md),
  [0421](0421-nl-3-keep-comments-crisp.md),
  [0537](0537-mark-result-nodiscard-on-the-type-not-on-functions.md).
