# 0374. SF.3: Use .h files for all declarations used in multiple source files

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

Maintainability and readability.

```cpp
// bad
// bar.cpp:
void bar() { cout << "bar\n"; }

// foo.cpp:
extern void bar();
void foo() { bar(); }
```

A maintainer of `bar` cannot find all declarations of `bar` if its type needs
changing. The user of `bar` cannot know whether the interface being used is
complete and correct. At best, the error messages come — late — from the linker.

The declaration written by hand in `foo.cpp` is a copy of an interface with no
link back to the thing it describes. Nothing keeps the copy in step with the
original, and nothing points a person changing the original at the copy. The
header exists so that there is one declaration, in one place, against which both
the definition and every use are checked.

## Decision

Give every entity used from more than one translation unit a declaration in a
`.h` file, and include that header wherever the entity is used.

- Never write an `extern` function or variable declaration in a `.cpp` in order
  to reach something defined in another `.cpp`.
- When an entity becomes used by a second translation unit, move its declaration
  into a header rather than duplicating it.
- Include the header both where the entity is defined and where it is used, so
  the compiler checks the definition and the uses against the same declaration —
  see
  [0376](0376-sf-5-a-cpp-file-must-include-the-h-file-s-that-defines-its-interface.md).
- An entity used from one translation unit only does not belong in a header at
  all; give it internal linkage instead, per
  [0386](0386-sf-22-use-an-unnamed-anonymous-namespace-for-all-internal-non-exported-entities.md).

## Rationale

A hand-written `extern` declaration is a second source of truth for a type, and
the language will not tell you when the two disagree — it will produce an
unresolved or, worse, a wrongly resolved symbol at link time. Routing every
cross-file use through one header turns "are these the same function?" from a
question about programmer discipline into a question the compiler answers.

The maintainer's half of the argument is the sharper one: with declarations
scattered, changing a signature means finding every copy, and there is no
reliable way to search for something whose spelling is exactly what you are
changing.

## Consequences

### Positive

- One declaration per entity, so a signature change is made once and checked
  everywhere.
- Interface errors surface at compile time rather than at link time.
- Reading a header shows what a component offers; grepping for `extern` no longer
  forms part of that.

### Negative

- It pushes towards headers that exist only to publish one function, and towards
  more files than the code strictly needs.
- Publishing a declaration in a header advertises it, so entities that were meant
  to be shared with exactly one other file become available to everything that
  includes the header. The rule offers no intermediate visibility.
- It says nothing about when a declaration becomes shared, so the migration from
  a file-local definition to a published header is a judgement made after the
  second use site already exists — that is, after the duplicate declaration has
  been written.

### Neutral

- The rule says nothing about how headers are grouped; one header per class, or
  per component, is a separate choice.
- A forward declaration used to break a dependency is still a declaration and
  still belongs to this rule, though it is often the right answer to
  [0380](0380-sf-9-avoid-cyclic-dependencies-among-source-files.md).

## Enforcement

- Flag declarations of entities defined in other source files that are not placed
  in a `.h`.

## Related

- [0373](0373-sf-2-a-h-file-must-not-contain-object-definitions-or-non-inline-function-definitions.md)
  — the complementary rule about what a header must not contain.
- [0376](0376-sf-5-a-cpp-file-must-include-the-h-file-s-that-defines-its-interface.md)
  — how the definition gets checked against the declaration.
- [0386](0386-sf-22-use-an-unnamed-anonymous-namespace-for-all-internal-non-exported-entities.md)
  — what to do with the entities that are not shared.
- [0020](0020-i-1-make-interfaces-explicit.md) — a header is the explicit form of
  an interface between files.
