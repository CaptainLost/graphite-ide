# 0398. SL.str.4: Use char* to refer to a single character

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

The variety of uses of `char*` in current code is a major source of errors.

```cpp
char arr[] = {'a', 'b', 'c'};

void print(const char* p)
{
    cout << p << '\n';
}

void use()
{
    print(arr);   // run-time error; potentially very bad
}
```

The array `arr` is not a C-style string, because it is not zero-terminated.
Nothing in the code says so: the initialiser looks like a string, the array
decays to `const char*` exactly as a string literal would, and `print` has no
way to tell the difference. The stream then reads past the end of the array
until it happens to find a zero byte.

The alternatives are the named ones from the rest of this subsection —
`zstring` and `czstring` for a zero-terminated sequence, `std::string` for an
owned one, and `std::string_view` for a referenced one.

## Decision

Let a bare `char*` mean a pointer to exactly one character, and use a named type
for everything else.

- Where a pointer refers to a single character, `char*` or `const char*` is the
  right spelling and no alias is needed.
- Where it refers to a zero-terminated sequence, write `zstring` or `czstring`.
- Where it refers to a sequence with a known length, write `std::string_view` or
  `span<char>`, which carry that length.
- Do not index or iterate through a `char*`; if the code does that, the
  parameter was never a pointer to one character.
- Do not initialise a character array with a braced list of characters and then
  pass it anywhere that expects a string.

## Rationale

This rule is what makes the previous one useful. Naming the zero-terminated case
`zstring` only distinguishes anything if the unnamed case means something else,
and the meaning left over for a bare `char*` is the narrowest one: one
character.

The example is worth keeping because the defect has no local symptom.
`print(arr)` compiles, runs, and usually prints the three characters plus
whatever follows them in memory; the bug is a missing zero byte in a declaration
several lines away, and no amount of care inside `print` could have detected it.

## Consequences

### Positive

- A bare `char*` acquires a single, narrow meaning, so a reader knows what it is
  without tracing call sites.
- Every sequence-shaped use is pushed onto a type that says whether it is
  terminated or measured.
- Indexing through a `char*` becomes a flag rather than an idiom, which is
  exactly what the enforcement rule checks.

### Negative

- The convention runs against decades of practice in which `char*` means
  "string", so it surprises readers and is contradicted by every C header the
  code includes.
- It is unenforceable across a boundary: a platform API taking `const char*`
  means a string regardless of what this record says.
- The `[]`-on-`char*` check produces false positives on legitimate
  single-character buffers and on code that has not yet been converted.
- Following it costs a dependency on the GSL aliases for the case that occurs
  most often, while the case this rule reserves the bare type for is rare.

### Neutral

- The rule assigns a meaning; it does not claim the single-character use is
  common or preferable.
- Where a single character is a value rather than a location, passing `char` by
  value is better than either alternative, and this rule does not address that.

## Enforcement

- Flag uses of `[]` on a `char*`.

## Related

- [0397](0397-sl-str-3-use-zstring-or-czstring-to-refer-to-a-c-style-zero-terminated-sequence-of-characters.md)
  — the companion rule that gives the sequence case its own names.
- [0396](0396-sl-str-2-use-std-string-view-or-gsl-span-char-to-refer-to-character-sequences.md)
  — the measured-sequence alternative.
- [0148](0148-r-2-in-interfaces-use-raw-pointers-to-denote-individual-objects-only.md)
  — the same principle for pointers generally; this is its character-typed case.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — why one
  type with four meanings is a bad interface.
