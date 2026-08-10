# 0396. SL.str.2: Use std::string_view or gsl::span<char> to refer to character sequences

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

`std::string_view` and `gsl::span<char>` provide simple and potentially safe
access to character sequences independently of how those sequences are allocated
and stored.

```cpp
vector<string> read_until(string_view terminator);

void user(zstring p, const string& s, string_view ss)
{
    auto v1 = read_until(p);
    auto v2 = read_until(s);
    auto v3 = read_until(ss);
    // ...
}
```

The point of the example is the three call sites. One function accepts a
zero-terminated `char*`, a `std::string`, and another view, without overloads
and without forcing any caller to allocate. A `const string&` parameter would
have made the first call allocate and copy; a `const char*` parameter would have
made the second call reach for `.c_str()` and would have discarded the known
length.

`std::string_view`, added in C++17, is read-only.

## Decision

Take a view, not a container, when a function only reads a character sequence.

- Declare a read-only character parameter as `std::string_view`.
- Declare a character parameter that will be written through as
  `gsl::span<char>`.
- Do not write `const std::string&` for a parameter that is only inspected, and
  do not overload a function on `const char*` and `const string&` to accept
  both.
- Do not store a view in a member or return one from a function unless the
  lifetime of the underlying characters is a documented part of the contract.

## Rationale

A view separates two questions that `const string&` conflates: what the function
does with the characters, and where the characters live. The signature that
takes a view answers only the first, which is the only one the function has any
business knowing.

The practical effect is that the caller stops paying. Every conversion a
`const string&` parameter forces is an allocation and a copy performed solely to
satisfy a type, and it happens at exactly the call sites that already had the
data in the right form.

## Consequences

### Positive

- One signature serves callers holding a `string`, a literal, a `char*`, or a
  subrange of any of them.
- Callers stop allocating to satisfy a parameter type.
- The length travels with the pointer, so length arguments and `strlen` calls
  disappear from the interface.

### Negative

- A view does not own, and says nothing about lifetime: a `string_view` into a
  temporary dangles as easily as a pointer, and looks safer than one. Returning
  a view from a function that took a `string` by value is a defect the type
  system does not catch.
- `std::string_view` is not guaranteed to be zero-terminated, so passing one on
  to a C API requires materialising a `string` — the copy the view was meant to
  avoid.
- `gsl::span<char>` is a GSL type, so the mutable half of this rule requires a
  dependency the standard library does not supply under that name.
- A view parameter accepts a subrange silently, so an off-by-one at the call
  site produces a well-formed call over the wrong characters.

### Neutral

- `string_view` being read-only is the whole reason
  [0401](0401-sl-str-11-use-gsl-span-char-rather-than-std-string-view-when-you-need-to-mutate-a-string.md)
  exists as a separate rule.
- Upstream gives no enforcement for this rule.

## Related

- [0395](0395-sl-str-1-use-std-string-to-own-character-sequences.md) — the
  owning counterpart; between them they cover the two roles.
- [0401](0401-sl-str-11-use-gsl-span-char-rather-than-std-string-view-when-you-need-to-mutate-a-string.md)
  — which of the two views to use when the characters are written.
- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the general
  rule; a character sequence is the commonest case of it.
- [0457](0457-f-24-use-a-span-t-or-a-span-p-t-to-designate-a-half-open-sequence.md)
  — the parameter-passing rule for spans generally.
