# 0058. F.60: Prefer T* over T& when no argument is a valid option

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

Numbered out of sequence in the source, where it sits between F.21 and F.22.
It states from the pointer side what
[0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) states
from the reference side, and together the two settle the codebase's convention
for optional arguments.

## Decision

Adopt F.60 as written. Transcribed in full:

> **Reason.** A pointer (`T*`) can be a `nullptr` and a reference (`T&`) cannot,
> there is no valid "null reference". Sometimes having `nullptr` as an
> alternative to indicated "no object" is useful, but if it is not, a reference
> is notationally simpler and might yield better code.

**Example.**

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

**Note.** It is possible, but not valid C++ to construct a reference that is
essentially a `nullptr` (e.g., `T* p = nullptr; T& r = *p;`). That error is very
uncommon.

**Note.** If you prefer the pointer notation (`->` and/or `*` vs. `.`),
`not_null<T*>` provides the same guarantee as `T&`.

**Enforcement.** Flag `???`

## Rationale

Adopted as written, and it completes the convention this directory has been
assembling from three directions:

- **F.60** — a pointer means the argument is optional.
- **I.12** ([0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md))
  — a required argument is a reference, since `not_null` is unavailable.
- **F.7** ([0046](0046-f-7-for-general-use-take-t-or-t-arguments-rather-than-smart-pointers.md))
  — a smart pointer means ownership moves.

Taken together: **reference means required, raw pointer means optional, smart
pointer means ownership.** That is the whole parameter-type decision procedure
for this codebase, and it is now written down in three records that agree.

The second Note's `not_null<T*>` alternative is unavailable here for the reason
given in [0019](0019-p-13-use-support-libraries-as-appropriate.md), which is why
the reference form is the one adopted.

## Consequences

### Positive

- Closes the convention: every parameter type in a signature now carries a
  defined meaning, readable without a comment.
- `std::optional` covers the by-value optional case, so the pointer form is
  needed only where the object is not owned by the caller's expression.

### Negative

- A pointer parameter obliges the callee to check for null on every path, and
  nothing enforces that it does. The convention states the obligation; it does
  not discharge it.
- The rule's own enforcement is `???` — the guidelines have not worked out how
  to check this, and neither has this project.

### Neutral

- No inventory. Distinguishing "pointer because optional" from "pointer because
  it was written that way" requires reading each function, and the convention
  applies going forward rather than retroactively.

## Related

- [0057](0057-f-21-to-return-multiple-out-values-prefer-returning-a-struct.md) —
  the previous rule in document order.
- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — the
  same convention from the reference side.
- [0046](0046-f-7-for-general-use-take-t-or-t-arguments-rather-than-smart-pointers.md)
  — the third leg: what a smart pointer parameter means.
