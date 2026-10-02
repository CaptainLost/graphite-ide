# 0362. T.124: Prefer to use standard-library TMP facilities

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Facilities defined in the standard — `conditional` and `enable_if` from
`<type_traits>`, `tuple` from `<tuple>`, `index_sequence` from `<utility>` — are
portable and can be assumed to be known. A reader who meets
`std::conditional_t` in a template does not have to work out what it does, and
a compiler that supports the standard supports it, often with an intrinsic that
is faster than any hand-written equivalent.

Upstream states the reason and leaves its example unwritten, so this is the
whole of what it supplies.

## Decision

Reach for the standard library's compile-time facilities before writing your
own.

- Use `<type_traits>` for type queries and transformations rather than defining
  equivalent traits.
- Use `std::tuple`, `std::index_sequence` and `std::apply` for the mechanics of
  working with packs.
- Prefer the `_t` and `_v` shorthands, which are the intended spelling.
- Where a needed facility is a small composition of standard ones, define it as
  a named alias over them rather than reimplementing it from scratch.
- Where C++20 is available, prefer its facilities — concepts and the standard
  concepts library — to the older traits they replace, `enable_if` in
  particular.

## Rationale

The standard facilities are the shared vocabulary: they need no documentation,
no tests and no maintenance, and a reviewer can check a line that uses them
without leaving the file. A hand-written trait has to be right for every
qualified, referenced and array type, and the standard ones already are — often
implemented by a compiler intrinsic that a portable version cannot match.

## Consequences

### Positive

- Portable across conforming toolchains, with no configuration.
- Familiar to any C++ reader, so the metaprogramming that remains is at least
  legible.
- Frequently implemented as intrinsics, so compile-time cost is lower than for
  an equivalent written in the language.

### Negative

- The standard lags the practice: `enable_if` is a standard facility that
  concepts have superseded, so "prefer the standard" and "prefer the modern
  mechanism" can point in different directions.
- Some facilities are clumsy enough that using them is worse than avoiding the
  metaprogramming altogether — deep `std::tuple` manipulation especially.
- The portability claim is only as good as the toolchain's conformance, and the
  newest additions are exactly the ones with uneven support.
- The standard set is incomplete, so a real design often ends up mixing standard
  pieces with local ones, and the seam has to be documented.

### Neutral

- This rule ranks the options; whether to metaprogram at all is a separate
  question.
- Where the standard set runs out, the next step is an existing library, not a
  local invention.

## Related

- [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md)
  — the general form of this preference.
- [0363](0363-t-125-if-you-need-to-go-beyond-the-standard-library-tmp-facilities-use-an-existing-library.md)
  — what to do when the standard set is not enough.
- [0358](0358-t-120-use-template-metaprogramming-only-when-you-really-need-to.md)
  — the prior question this rule assumes has been answered.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — the principle
  behind both.
