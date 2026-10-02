# 0397. SL.str.3: Use zstring or czstring to refer to a C-style, zero-terminated, sequence of characters

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

Readability, and statement of intent. A plain `char*` can be a pointer to a
single character, a pointer to an array of characters, a pointer to a C-style
zero-terminated string, or even a pointer to a small integer. Distinguishing
these alternatives prevents misunderstandings and bugs.

```cpp
void f1(const char* s); // s is probably a string
```

All we know is that it is supposed to be the `nullptr` or to point to at least
one character. The alternatives say which of the four meanings is intended:

```cpp
void f1(zstring s);     // s is a C-style string or the nullptr
void f1(czstring s);    // s is a C-style string constant or the nullptr
void f1(std::byte* s);  // s is a pointer to a byte (C++17)
```

Do not convert a C-style string to `string` unless there is a reason to. Like
any other plain pointer, a `zstring` should not represent ownership.

There are billions of lines of C++ in existence, most of which use `char*` and
`const char*` without documenting intent. They are used in a wide variety of
ways, including to represent ownership and as generic pointers to memory instead
of `void*`. It is hard to separate these uses, so this guideline is hard to
follow. It is also one of the major sources of bugs in C and C++ programs, so it
is worthwhile to follow wherever feasible.

## Decision

Write down which of `char*`'s meanings is intended, at every interface that uses
one.

- Use `zstring` for a mutable C-style zero-terminated string and `czstring` for
  a constant one.
- Use `std::byte*` for a pointer to raw bytes that are not characters, and a
  plain `char*` only for a pointer to one character.
- Do not use a `zstring` or `czstring` to convey ownership; these are plain
  pointers and the aliases change nothing about that.
- Do not convert an incoming C-style string to `std::string` merely because it
  arrived as a pointer; convert when the sequence is to be owned or modified.
- Where the sequence is indexed rather than passed on whole, convert to
  `string_view` or `span<char>` so the extent is known.

## Rationale

The alias carries no run-time information and generates no code; its entire
value is that the four incompatible meanings of `char*` become four different
names. That makes a wrong call site visible in review, and it makes a right one
provable to a reader who has only the declaration.

Upstream is unusually frank that this rule is hard to follow, because the
surrounding world of C and C++ code does not follow it. Recording that honestly
matters more than pretending the convention can be applied everywhere: it is
applied at the interfaces you own, and the sprawl of meanings is handled at the
boundary.

## Consequences

### Positive

- A declaration states which of the four meanings of `char*` is intended,
  without a comment.
- The distinction between a character pointer and a byte pointer becomes
  visible in signatures.
- The aliases cost nothing at run time and require no new types to be
  introduced.

### Negative

- `zstring` and `czstring` are GSL aliases, so following this rule literally
  means taking that dependency, and taking it in headers where the aliases
  appear in public signatures.
- The aliases are aliases: nothing stops a `char*` being passed where a
  `czstring` is expected, so the convention is documentation, not enforcement.
- The surrounding ecosystem is full of undocumented `char*`, so a codebase
  applying this rule still meets bare `char*` at every external boundary, and
  every such boundary needs a decision.
- Neither alias says anything about length or ownership, so the two most
  dangerous properties of a C string remain undocumented.

### Neutral

- The rule is about naming intent, not about preferring C strings; where a
  sequence is owned, [0395](0395-sl-str-1-use-std-string-to-own-character-sequences.md)
  still applies.
- Zero-terminated strings are the explicitly permitted exception to the general
  prohibition on passing an array as a single pointer.

## Enforcement

- Flag uses of `[]` on a `char*`.
- Flag uses of `delete` on a `char*`.
- Flag uses of `free()` on a `char*`.

## Related

- [0398](0398-sl-str-4-use-char-to-refer-to-a-single-character.md) — the other
  half of the same convention.
- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — states
  zero-terminated strings as the exception, with these same aliases.
- [0458](0458-f-25-use-a-zstring-or-a-not-null-zstring-to-designate-a-c-style-string.md)
  — the parameter-passing form of this rule.
- [0399](0399-sl-str-5-use-std-byte-to-refer-to-byte-values-that-do-not-necessarily-represent-characters.md)
  — the byte-pointer alternative named in the example.
