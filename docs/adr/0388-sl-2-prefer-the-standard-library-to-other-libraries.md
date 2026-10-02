# 0388. SL.2: Prefer the standard library to other libraries

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

More people know the standard library than know any other library. It is more
likely to be stable, well maintained, and widely available than your own code or
most other libraries.

That is the whole of the upstream reason, and each clause is doing separate
work. *Known* means a reader of the code does not have to learn anything to
follow it. *Stable* means the interface will not be redesigned under you.
*Well maintained* means defects get fixed by people whose job that is.
*Widely available* means it is already present on every platform the compiler
supports, with no build, no submodule, and no licence question.

The standard library is also the only library the guidelines can assume, which
is why the rest of this section is written in terms of `std::string`,
`std::vector`, `std::array` and iostreams rather than any third-party
equivalent.

## Decision

When more than one library offers the facility you need, take the standard one.

- Use `std::vector`, `std::array`, `std::string`, `std::string_view`,
  `std::optional`, the `<algorithm>` and `<chrono>` facilities and the rest of
  the standard library in preference to a third-party or hand-written
  equivalent.
- Do not add a dependency whose value is a nicer spelling of something the
  standard library already does.
- Where a third-party library is chosen over a standard facility, the reason
  must be a concrete capability or measured cost, not taste.
- Where a non-standard type is genuinely better, confine it behind an interface
  so it does not spread through headers that had no need of it.

## Rationale

Familiarity is the argument that outweighs the others. A `std::vector` in a
diff needs no explanation to any C++ programmer alive; a custom
`SmallVectorImpl` needs a paragraph, and gets one wrong. The standard library is
the shared vocabulary, and picking a different word costs every future reader.

Availability matters second: standard facilities cost nothing to obtain, so the
comparison is not "library A versus library B" but "library A versus something
you already have".

## Consequences

### Positive

- Code reads without a glossary, and new contributors start from what they
  already know.
- No build, submodule, licence or upgrade cost for the facility used.
- Interface stability is guaranteed by the standardisation process rather than
  by a maintainer's goodwill.
- Types interoperate: standard containers, algorithms and ranges compose with
  each other by construction.

### Negative

- The standard library is conservative and late. Facilities arrive years after
  they are needed, and a third-party library often has the better design first.
- Standard implementations are general, so a specialised alternative can be
  measurably faster or smaller for a specific workload; this rule pushes against
  taking it.
- Some standard components are widely regarded as mistakes, and this rule as
  written offers no exemption for them.
- Diagnostics and debugging inside standard library headers are notoriously
  poor, which is a real day-to-day cost of preferring them.

### Neutral

- Depending on a non-standard library for something the standard library does
  not provide at all is not a violation; the rule is about choosing between
  overlapping options.
- The guidelines' own GSL types such as `span` and `not_null` are treated as
  standard-adjacent throughout this section.

## Related

- [0387](0387-sl-1-use-libraries-wherever-possible.md) — use a library at all;
  this record says which one.
- [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md)
  — the same preference, stated in the expressions section.
- [0008](0008-p-2-write-in-iso-standard-c.md) — the same instinct applied to the
  language rather than the library.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — the philosophy
  rule behind both.
