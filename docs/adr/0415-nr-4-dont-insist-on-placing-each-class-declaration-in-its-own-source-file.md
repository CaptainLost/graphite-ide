# 0415. NR.4: Don't insist on placing each class declaration in its own source file

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NR (Non-Rules and myths), transcribed 2026-08-10

## Context

The NR section collects rules that are popular somewhere but that the
guidelines deliberately do not recommend. This record adopts the rejection, not
the practice being rejected.

The practice here is the one-class-per-file convention, inherited from languages
that require it. The number of files that results is hard to manage and can slow
down compilation. More fundamentally, an individual class is rarely a good
logical unit of maintenance and distribution: classes that are designed
together, changed together and used together are separated by the convention for
no reason other than that they are separate classes.

The alternative offered upstream is to use namespaces containing logically
cohesive sets of classes and functions — that is, to let the file follow the
cohesive group rather than the class.

Upstream marks this rule's example as a placeholder, so there is no code to
transcribe.

## Decision

Do not require a separate header and source file for every class, and do not
reject a file because it declares more than one.

- Let a file hold a logically cohesive group: a class and the free functions
  that support it, a small family of related types, an enumeration and the
  operations on it.
- Split a file when it stops being cohesive, when its compile cost becomes a
  problem, or when part of it needs to be included independently — not when it
  reaches two classes.
- Keep a class in its own file when it is large or is the unit others depend on.
  The objection is to the insistence, not the arrangement.
- Use a namespace, not a file, as the primary grouping mechanism. Helper
  functions belong in the namespace of the class they support, per
  [0044](0044-c-5-place-helper-functions-in-the-same-namespace-as-the-class-they-support.md).

## Rationale

The convention optimises for finding a class by name, which the toolchain does
better than the directory layout does, and pays for it in the thing that
actually costs — the number of headers each translation unit pulls in, and the
number of files that must be opened to read one idea.

The deeper point is about units. Maintenance, review, distribution and
deprecation all operate on cohesive groups, and a rule that forces the file
boundary to fall between every pair of classes guarantees that the file boundary
is not that group.

## Consequences

### Positive

- A cohesive group can be read in one place, including the free functions that
  are part of its interface.
- Fewer, larger headers mean fewer include edges, which is one of the few
  reliable levers on C++ build time.
- File boundaries can be drawn where they carry information, rather than being
  determined in advance.

### Negative

- Without the mechanical rule, where to split becomes a judgement made
  repeatedly, and inconsistently, across a codebase.
- Locating a class by guessing its filename stops working, so it depends on
  tooling — which not every editing context has.
- A grouped header recompiles every dependant when any class in it changes, so
  the build-time argument can run the other way for a volatile group.
- A file that grows a class at a time, with nobody deciding it has stopped being
  cohesive, ends up worse than the convention would have produced.
- Build systems that enumerate sources make every new file a small
  administrative cost, which is a weak argument that nonetheless pushes in the
  direction of grouping classes that do not belong together.

### Neutral

- Nothing here is an argument for large files. It is an argument that class
  count is the wrong measure of a file's size.
- The rule concerns declarations and their placement, not the separation of
  interface from implementation.

## Related

- [0044](0044-c-5-place-helper-functions-in-the-same-namespace-as-the-class-they-support.md)
  — the alternative upstream offers: group by namespace, not by file.
- [0410](0410-a-2-express-potentially-reusable-parts-as-a-library.md) — the unit
  of maintenance and distribution is a library, not a class.
- [0042](0042-c-3-represent-the-distinction-between-an-interface-and-an-implementation-using-a-class.md)
  — the separation that does matter, which is not a file boundary.
- [0409](0409-a-1-separate-stable-code-from-less-stable-code.md) — a genuine
  reason to split, as against an arithmetical one.
