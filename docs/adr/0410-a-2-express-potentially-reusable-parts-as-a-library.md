# 0410. A.2: Express potentially reusable parts as a library

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section A (Architectural ideas), transcribed 2026-08-10

## Context

Upstream states this rule as a title with an empty Reason heading, so the only
substantive material is a note defining the term. A library is a collection of
declarations and definitions maintained, documented and shipped together. It
could be a set of headers — a "header-only library" — or a set of headers plus
a set of object files. It can be linked into a program statically or
dynamically, or, if it is header-only, simply `#include`d.

That definition is the content of the rule. What makes something a library is
not how it is compiled but that it is maintained, documented and shipped as one
thing: a unit with a name, a boundary, and an identity separate from any
particular program that uses it.

The rule is therefore about packaging and ownership rather than about
compilation. Calling a part a library is a commitment to maintain it, document
it and ship it as a whole, and a part that is only ever built as an inseparable
piece of one program has not been made into one, whatever its directory is
called.

## Decision

When a part of the system is potentially useful to more than one program,
express it as a library rather than leaving it embedded in an application.

- Give it a boundary: its own headers, its own namespace, and an interface that
  does not name the application that happens to use it today.
- Maintain, document and ship it as a unit. A directory of files that are only
  ever built as part of one executable is not a library, whatever it is called.
- Choose header-only or headers-plus-objects on the merits of the code, not as
  part of deciding whether it is a library at all; the distinction does not
  change what the boundary means.
- Do not let application concerns leak back across the boundary. The dependency
  runs from the program to the library and never the other way.

## Rationale

The note carries the whole argument: "maintained, documented, and shipped
together" is a statement about ownership, not about linkage. Naming a part a
library is what forces someone to decide what belongs in it and what its
interface is, and those two decisions are the reuse.

Reuse is the stated goal, but the boundary pays for itself before any second
user exists — it is what makes the part testable on its own, and what stops the
application's assumptions from being written into it.

## Consequences

### Positive

- The reusable part has an interface that was designed rather than discovered,
  because a library boundary forces the question.
- It can be built and tested without the program that uses it, which is what
  makes a test suite against it possible at all.
- Its dependencies are visible, since anything it needs must be declared rather
  than assumed present in the enclosing program.

### Negative

- Upstream gives no Reason for this rule, only a definition, so the record's
  justification is inferred rather than transcribed.
- A library boundary is a commitment: its interface becomes something to keep
  stable, and changes that would have been local now ripple to every user.
- "Potentially reusable" is unbounded — almost anything is potentially
  reusable — so the rule as stated would justify extracting far too much.
- Generalising a part before a second user exists usually produces the wrong
  generalisation, at the cost of an extra layer.

### Neutral

- Header-only and compiled libraries are both libraries under this definition;
  the trade-off between them is a separate matter.
- Whether a library is versioned and released independently is not settled here.

## Related

- [0409](0409-a-1-separate-stable-code-from-less-stable-code.md) — the same
  boundary argument, motivated by rate of change rather than reuse.
- [0411](0411-a-4-there-should-be-no-cycles-among-libraries.md) — the constraint
  on how libraries may depend on each other.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — the consuming side
  of the same idea.
- [0044](0044-c-5-place-helper-functions-in-the-same-namespace-as-the-class-they-support.md)
  — the grouping mechanism inside a library's boundary.
