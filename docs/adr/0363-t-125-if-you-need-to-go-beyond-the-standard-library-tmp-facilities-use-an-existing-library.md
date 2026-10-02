# 0363. T.125: If you need to go beyond the standard-library TMP facilities, use an existing library

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Getting advanced template metaprogramming facilities right is not easy, and
using a library makes you part of a — hopefully supportive — community. Write
your own advanced metaprogramming support only if you really have to.

Upstream states the reason and leaves its example unwritten, so this is the
whole of what it supplies. The rule sits at the end of a sequence: do not
metaprogram unless you must; when you must, use the standard facilities; when
those run out, use a library that already exists; and only then write your own.

## Decision

When the standard facilities are not enough, adopt an established library
rather than building the machinery yourself.

- Survey the existing metaprogramming libraries before starting, and choose one
  whose vocabulary matches the problem.
- Use it as it is intended to be used, rather than extracting fragments of it
  into local copies that will not be updated.
- Where a small local facility is genuinely needed, build it out of standard
  and library components rather than from first principles, and confine it to
  one named header.
- Treat writing new advanced metaprogramming support as the last option, taken
  deliberately and recorded.

## Rationale

The hard part of a metaprogramming facility is not making it work for the case
that prompted it, but making it work for every qualified, referenced, array and
incomplete type that will eventually reach it — and that long tail is exactly
what an established library has already paid for. A community around the
library also supplies the two things a local implementation never has:
documentation somebody else wrote, and other people who have hit the bug first.

## Consequences

### Positive

- The corner cases have been found and fixed by more users than a local
  implementation will ever have.
- Documentation, idioms and answers to common questions already exist.
- Effort goes into the problem rather than into rebuilding infrastructure.

### Negative

- A dependency is a real, ongoing cost: it must be acquired, versioned, built,
  updated and licensed, and metaprogramming libraries are among the heaviest
  contributors to compile time.
- Adopting one imports its whole vocabulary, so readers must learn a second
  idiom alongside the standard one.
- Diagnostics from a deep library are worse than from local code, because the
  instantiation stack now runs through someone else's abstractions.
- "Existing library" is not a guarantee of maintenance; a library that is
  abandoned after adoption leaves the same problem plus a migration.

### Neutral

- The rule ranks options; it does not say which library, and upstream names
  none.
- A local facility is not forbidden, only demoted below the alternatives.

## Related

- [0362](0362-t-124-prefer-to-use-standard-library-tmp-facilities.md) — the
  option that comes before this one.
- [0358](0358-t-120-use-template-metaprogramming-only-when-you-really-need-to.md)
  — the question that should be answered before either.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — the general
  principle that a good library beats hand-written support code.
