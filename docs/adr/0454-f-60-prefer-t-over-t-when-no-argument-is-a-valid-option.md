# 0454. F.60: Prefer T* over T& when "no argument" is a valid option

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

A pointer can be `nullptr` and a reference cannot; there is no valid null
reference. Sometimes having `nullptr` available as a way of indicating "no
object" is useful, but where it is not, a reference is notationally simpler and
might yield better code.

```cpp
string zstring_to_string(zstring p) // zstring is a char*; that is a C-style string
{
    if (!p) return string{};    // p might be nullptr; remember to check
    return string{p};
}

void print(const vector<int>& r)
{
    // r refers to a vector<int>; no check needed
}
```

It is possible, though not valid C++, to construct a reference that is
essentially a null pointer — `T* p = nullptr; T& r = *p;` — but that error is
very uncommon.

If you prefer the pointer notation, using `->` and `*` rather than `.`,
`not_null<T*>` provides the same guarantee as `T&`.

## Decision

Choose between `T*` and `T&` by whether the absence of an argument is
meaningful.

- Use `T&` when the function requires an object; the caller then cannot fail to
  supply one and the body needs no check.
- Use `T*` when "no object" is a legitimate thing for a caller to say, and check
  for null on entry.
- Do not take a `T*` merely to permit a null the function immediately rejects;
  that is a `T&`, or a `not_null<T*>` if pointer notation is wanted.
- Do not construct a reference by dereferencing a pointer that might be null in
  order to satisfy a `T&` parameter.
- Consider `std::optional` where the optional thing is a value rather than an
  identity, and the reference or pointer was only a way of expressing absence.

## Rationale

The choice of parameter type decides where the null check lives, and there are
only two honest options: once, in the callee, or never, because the language
made it impossible. `T&` buys the second, and it buys it for every call site at
once, which is what makes the notational simplicity argument secondary. The rule
is stated as a preference for `T*` in the optional case precisely because the
alternative — a reference that the caller must somehow conjure from nothing — is
how the invalid-reference trick gets written.

## Consequences

### Positive

- Optionality becomes visible in the signature rather than in documentation.
- A reference parameter removes the null check entirely, along with the branch
  and the question of what to do on the null path.
- Callers of a `T&` function cannot get the call wrong in the null direction.
- The choice composes with `not_null<T*>`, which gives the reference's guarantee
  to code that wants pointer notation.

### Negative

- A `T*` parameter says only that null is possible, not what the function does
  when it receives one, so the contract is still incomplete and needs stating.
- Requiring a check on entry to every optional-pointer function reintroduces
  exactly the defensive checking that `not_null` exists to remove, and nothing
  enforces that the check happens.
- Rebinding is bundled into the choice: a caller who wants an optional argument
  gets a mutable, rebindable pointer whether or not that was wanted.
- Some callers hold objects only as pointers, so a `T&` parameter pushes a
  dereference — and the possibility of undefined behaviour — up to the call
  site.

### Neutral

- The invalid-reference construction is possible but is not valid C++, and the
  rule treats it as a rarity rather than a hole.
- The rule is about parameters; the same distinction for return values is
  governed separately.

## Related

- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — the
  other half of the same decision: what a non-optional pointer should be.
- [0456](0456-f-23-use-a-not-null-t-to-indicate-that-null-is-not-a-valid-value.md)
  — `not_null<T*>` as the pointer-notation equivalent of `T&`.
- [0448](0448-f-16-for-in-parameters-pass-cheaply-copied-types-by-value-and-others-by-reference-to-const.md)
  — where the same advice appears as a note about optional values.
- [0444](0444-f-7-for-general-use-take-t-or-t-arguments-rather-than-smart-pointers.md)
  — the rule that leaves this choice open, and defers to this record.
