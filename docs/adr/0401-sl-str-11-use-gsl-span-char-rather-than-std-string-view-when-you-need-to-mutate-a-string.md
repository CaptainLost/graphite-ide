# 0401. SL.str.11: Use gsl::span<char> rather than std::string_view when you need to mutate a string

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

`std::string_view` is read-only. That is the whole of the upstream reason;
example and note are left as placeholders. What it means in practice is that
`string_view` is not a view of characters, it is a view of characters you
promise not to change: its `data()` returns `const char*` and all its element
access is const.

So the two view types in this subsection are not interchangeable. The general
rule to refer to a character sequence through a view — recorded as
[0396](0396-sl-str-2-use-std-string-view-or-gsl-span-char-to-refer-to-character-sequences.md)
— names both, and this rule says which of them applies: `string_view` when the
function reads, `gsl::span<char>` when it writes.

The one thing upstream does supply for this rule is its enforcement, and it is
unusually strong: the compiler flags attempts to write through a `string_view`,
so the failure mode is a diagnostic rather than a bug.

## Decision

Choose the character view by whether the characters are written.

- Declare a parameter that only reads as `std::string_view`.
- Declare a parameter that modifies the characters in place as
  `gsl::span<char>`.
- Do not obtain a mutable pointer from a `string_view` by casting away `const`
  in order to write through it.
- Do not take a `std::string&` merely to gain write access to the characters,
  where the function does not resize or reassign the string; that constrains the
  caller to one storage type for no reason.
- Where the operation changes the length of the sequence, neither view will do
  — take the `std::string` itself, because that is a change of ownership-level
  state, not of characters.

## Rationale

The distinction being drawn is between modifying a sequence and modifying a
container. A view can express the first and cannot express the second, and
picking the view by that criterion keeps the signature honest about which one
the function does.

`string_view`'s read-only nature is a feature being used here rather than a
limitation being worked around: a signature taking `string_view` is a compiler-
checked promise, so choosing the right view type also documents the function.

## Consequences

### Positive

- The signature says whether the characters are modified, and the compiler holds
  the read-only case to it.
- Callers keep their choice of storage; a mutable subrange of any buffer can be
  passed.
- The mistake this rule prevents is caught at compile time rather than found by
  reading.

### Negative

- `gsl::span<char>` is a GSL type. Following this rule means taking that
  dependency, and taking it in public headers, where the alternative —
  `std::span<char>`, available since C++20 — is not what the guideline names.
- Two view types for one concept is a real cost in a codebase: readers must know
  both, and a function whose mutability changes needs its signature changed
  along with every call site's expectations.
- Neither view can grow or shrink the sequence, so the rule leaves the common
  case of in-place editing that changes length without an answer.
- A mutable view over characters carries all of a view's lifetime hazards with
  none of a view's read-only safety.

### Neutral

- The rule is a refinement of
  [0396](0396-sl-str-2-use-std-string-view-or-gsl-span-char-to-refer-to-character-sequences.md),
  not an independent decision; it selects between the two types that record
  offers.
- Upstream leaves the example and note as placeholders, so the record's
  substance is the reason and the enforcement.

## Enforcement

- The compiler flags attempts to write to a `string_view`, so the read-only half
  of the rule is enforced by the language rather than by a checker.

## Related

- [0396](0396-sl-str-2-use-std-string-view-or-gsl-span-char-to-refer-to-character-sequences.md)
  — the rule this one refines.
- [0395](0395-sl-str-1-use-std-string-to-own-character-sequences.md) — what to
  take instead when the sequence's length changes.
- [0313](0313-con-3-by-default-pass-pointers-and-references-to-consts.md) — the
  same reasoning applied to pointers and references generally.
- [0207](0207-es-50-dont-cast-away-const.md) — rules out the shortcut this
  record prohibits.
