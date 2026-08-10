# 0403. SL.io.1: Use character-level input only when you have to

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

Unless you genuinely deal with individual characters, character-level input
leads to user code performing potentially error-prone and potentially
inefficient composition of tokens out of characters.

```cpp
char c;
char buf[128];
int i = 0;
while (cin.get(c) && !isspace(c) && i < 128)
    buf[i++] = c;
if (i == 128) {
    // ... handle too long string ....
}
```

Better, and much simpler and probably faster:

```cpp
string s;
s.reserve(128);
cin >> s;
```

and the `reserve(128)` is probably not worthwhile.

The comparison is not only about length. The first version has a fixed buffer, a
manual index, a bound that must be repeated in three places, an
easy-to-get-wrong loop condition, and a truncation case the caller has to handle
because the code cannot represent a longer word. The second has none of those,
because the library already knows how to read a word into a growable string.

## Decision

Read at the level of the thing you actually want.

- Extract into a `std::string`, a numeric type, or a user-defined type with an
  `operator>>`, rather than reading characters and assembling them.
- Use `std::getline` where the unit is a line.
- Reserve character-level reads — `get`, `peek`, `putback` — for cases where the
  individual character is the subject, such as a lexer or a protocol parser that
  genuinely dispatches on one character at a time.
- Do not read into a fixed character array as a step towards building a string.
- Do not add a `reserve` to an input path without a measurement showing it
  matters.

## Rationale

Hand-assembled token loops are where the fixed-size buffer gets back into code
that had otherwise stopped using them. The buffer brings its bound, the bound
brings a truncation case, and the truncation case is either mishandled or
handled by code nobody tests — all to reproduce what `operator>>` already does.

The performance instinct that motivates such loops usually points the wrong way:
the library's extraction operates on the stream buffer directly, while the
character loop pays per-character stream overhead.

## Consequences

### Positive

- Input code shrinks to the operation actually intended, and the buffer, index
  and bound disappear with it.
- Overflow and truncation cases vanish rather than being handled.
- The library's extraction is usually faster than the equivalent character loop.

### Negative

- Extraction hides the tokenisation rules, so a reader who needs to know exactly
  which characters are consumed as whitespace must go to the standard.
- `operator>>` into a `std::string` is unbounded, which is a denial-of-service
  concern on untrusted input that the explicit 128-character loop did at least
  address.
- Genuine lexers still need character-level input, and the boundary between
  "genuinely deals with individual characters" and "assembles tokens" is a
  judgement, not a test.
- Extraction reports failure through stream state, which must still be checked;
  the shorter code does not remove that obligation, only its visibility.

### Neutral

- Upstream gives no enforcement for this rule.
- The rule is about the granularity of reads, not about which I/O library is
  used.

## Related

- [0404](0404-sl-io-2-when-reading-always-consider-ill-formed-input.md) — the
  obligation that survives the simplification.
- [0405](0405-sl-io-3-prefer-iostreams-for-i-o.md) — the library whose
  extraction operators this rule assumes.
- [0395](0395-sl-str-1-use-std-string-to-own-character-sequences.md) — why the
  destination is a `string` rather than a `char` buffer.
- [0239](0239-per-2-dont-optimize-prematurely.md) — the `reserve(128)` that is
  probably not worthwhile.
