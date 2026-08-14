# 0481. Lex one line at a time carrying a resumable state

Status: proposed
Date: 2026-08-13

## Context

Syntax colouring needed a lexer for `TextEditorWidget` to call from
`DrawRowText`, which issues one `AddText` per token run. The widget already
draws by row: `ImGuiListClipper` skips whatever is scrolled off screen, and the
gutter, the caret and the selection are all computed a row at a time from a
`RowSlice`. A lexer built to hand back one token list for a whole file would be
answering a question nothing asks — the rows nobody looks at this frame still
need lexing to produce that list, and every one of them is relexed on the next
keystroke because the file, not the row, is the unit of work.

A file being edited is also not static. `SyntaxCache` exists because a line's
colours depend on the state every line above it left behind — a block comment
opened at line 0 still colours line 40 000 — and that dependency has to survive
retyping a single character without re-deriving the whole file's coloured
output from scratch.

C++ has three constructs that do not fit inside one line: a block comment, a
raw string, and a backslash-continued string or character literal. Whatever
the lexer's unit, these cross whatever boundary the unit draws, and something
has to carry the fact that a line began inside one.

## Decision

`Lexer::LexLine` takes one line, entered in a `LineState`, and returns the
`LineState` the next line begins in:

```cpp
virtual LineState LexLine(std::string_view line, const LineState& entry,
                          std::vector<Token>& out) const = 0;
```

`LineState` is a value — an opaque `Mode` word plus a string for a resume
condition too wide to fit in it — not a cursor into a stream or a handle the
lexer keeps for itself. It deliberately does not name the constructs it
carries: see [0484](0484-keep-cross-line-lexer-state-opaque.md). `SyntaxCache` stores one `LineState` per line (`m_EntryStates`), and an
edit invalidates from the touched line down by dropping every entry state at
or below it; the states above are untouched because nothing below an edit can
change what a line above it started in. Re-lexing after an edit is lazy: only
`ExtendTo` runs, and only up to whatever line `TokensOf` is next asked for —
typically the rows the clipper is about to draw. A change near the top of a
large file does not force a pass over the rest of it because nothing below
the visible rows is ever extended to, not because a comparison against the
previously cached state stops it. `LineState` equality exists (`SyntaxCache`
needs it storable and comparable per the Rationale below) but nothing in
`SyntaxCache` calls it to short-circuit a re-lex today.

## Rationale

Two properties were needed together, and a whole-file token list gives up the
first while a lexer with hidden internal state gives up the second:

- Lexing must be payable per row, so that scrolling and typing cost work
  proportional to what changed or what is drawn, not to the file's length.
- The carried state must be a comparable value, so `SyntaxCache` can store
  one per line and let two documents each hold their own without sharing a
  lexer instance. Equality is part of that value-ness — a state a lexer
  hands back has to be comparable for `SyntaxCache` to store it in a
  `std::vector` and reason about it at all — even though nothing calls it to
  short-circuit a re-lex today.

`LineState` is what makes both true at once: it is small enough to store one
per line without concern, and it is exactly the information a construct
crossing a line boundary needs to resume correctly on the next one.

## Consequences

### Positive

- Drawing costs lexing proportional to the rows the clipper actually visits,
  not the file's length.
- An edit invalidates from the touched line down and nothing is re-lexed
  until it is next asked for, so a change near the top of a large file does
  not force a pass over the rest of it — lazily, by extending only as far as
  `TokensOf` is called, not by comparing states to detect the file has
  settled back down.
- `Lexer` is stateless and shared (`LexerFor` returns one instance for the
  program's lifetime); every document's `SyntaxCache` calls the same lexer
  with its own state, so nothing about concurrent documents needs guarding.
- The unit tests exercise `LexLine` directly, one line and one `LineState` at
  a time, without needing a `TextBuffer` or a whole file on disk.

### Negative

- Every construct that can span a line boundary must be named as a
  `Continuation` and handled explicitly on both ends — entering `LexLine`
  and leaving it. A construct the lexer does not yet model (this stage only
  covers block comments, raw strings, and backslash-continued string and
  character literals) simply colours wrong across the boundary rather than
  failing loudly.
- `SyntaxCache` carries a `LineState` per line whether or not the file uses
  any construct that needs one, which is a small, constant memory cost paid
  by every open document.
- Correctness now depends on `SyntaxCache` calling `LexLine` with the right
  entry state and invalidating from the right line; a lexer that ignores its
  `entry` parameter, or a caller that invalidates the wrong line, produces
  colours that are locally sensible and globally wrong, which is a harder bug
  to notice than a crash.

### Neutral

- `LineState` supports equality because a state a lexer hands back has to be
  comparable to be stored and reasoned about as a value; a `Continuation`
  that carries data participating in that equality (the raw-string
  delimiter) has to be exact, not merely present, or two different raw
  strings would be treated as resuming the same way. `SyntaxCache` does not
  currently call that equality to stop a re-lex early — see Decision.
- `SyntaxCache` feeds `LexLine` tab-expanded text, not the document's own
  bytes: `Token::From`/`To` are offsets into the expanded line, because that
  is what the editor draws and what its column arithmetic measures. This is
  deliberate — it is what lets `DrawRowText` slice the same expanded string
  it already built for the row without a second mapping — but it means a
  token's offsets are not offsets into the document. A later stage asking
  "what token is at this document offset" (for example, a semantic pass or
  a click-to-definition lookup) will need to map a document byte offset to
  an expanded-line offset, or vice versa, before it can use `Token::From`/
  `To` directly.

## Alternatives Considered

### Lex the whole file into one token list

Simplest to reason about: one call, one vector, nothing to resume. Rejected
for the two reasons in Context — most of a large file's tokens are for rows
the clipper never draws this frame, and the whole list is rebuilt on every
keystroke because there is no per-line boundary to invalidate from. It also
does not remove the line-crossing problem, only hides it inside one function
instead of exposing it as `LineState`.

### Let the lexer hold state across calls

`LexLine(std::string_view line)` with no `entry` parameter, the lexer
remembering where it left off internally. Fewer parameters at the call site,
but the state is no longer a value: `SyntaxCache` could not store one entry
state per line, could not compare two states for equality to short-circuit a
re-lex, and two documents could not lex concurrently against one shared
`Lexer` instance without the second document's calls corrupting the first's
position. `LexerFor` returning one stateless, shared lexer per language is a
direct consequence of rejecting this option.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — `SyntaxCache` and
  `Lexer` live in Graphite, not Core; see
  [0482](0482-keep-the-language-layer-in-graphite-rather-than-core.md).
- `Graphite/src/Lang/Syntax/Token.h` — `LineState` and its `Continuation`
  cases.
- `Graphite/src/Lang/Syntax/SyntaxCache.h` — the per-document cache this
  record's shape makes possible.
