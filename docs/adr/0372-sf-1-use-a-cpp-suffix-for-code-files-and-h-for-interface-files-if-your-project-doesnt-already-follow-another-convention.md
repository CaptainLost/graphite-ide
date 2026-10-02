# 0372. SF.1: Use a .cpp suffix for code files and .h for interface files if your project doesn't already follow another convention

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

It is a longstanding convention. But consistency is more important, so if a
project already uses something else, follow that.

The convention reflects a common use pattern. Headers are more often shared with
C, to compile as both C++ and C, and C typically uses `.h`; it is easier to name
all headers `.h` than to have a different extension for just those headers
intended to be shared. Implementation files, on the other hand, are rarely shared
with C and so should be distinguished from `.c` files, which makes it best to
name all C++ implementation files something else, such as `.cpp`.

The specific names `.h` and `.cpp` are not required — they are recommended as a
default. Other names are in widespread use: `.hh`, `.C`, `.cxx`. They are used
equivalently. Throughout these records `.h` and `.cpp` are shorthand for header
and implementation file, even where an actual extension differs. An IDE, if one
is in use, might have strong opinions about suffixes.

```cpp
// foo.h:
extern int a;   // a declaration
extern void foo();

// foo.cpp:
int a;   // a definition
void foo() { ++a; }
```

`foo.h` provides the interface to `foo.cpp`. Global variables are best avoided;
that is a separate rule, and the example is about file roles rather than about
the variable.

```cpp
// bad
// foo.h:
int a;   // a definition
void foo() { ++a; }
```

`#include` that header twice in a program and the linker reports two
one-definition-rule violations.

## Decision

Name interface files `.h` and implementation files `.cpp`, and do not introduce a
third suffix.

- A `.h` file holds the interface: declarations, class definitions, templates,
  and the definitions that are permitted to be repeated in every translation
  unit.
- A `.cpp` file holds the implementation: the definitions that must exist exactly
  once.
- Do not name a C++ implementation file `.c`, and do not add further suffixes —
  `.hpp`, `.inl`, `.ipp`, `.tpp` — alongside the two already chosen.
- Where a project has already standardised on different extensions, keep them and
  apply them consistently; the pair matters more than the letters.

## Rationale

The suffix is the cheapest possible signal of a file's role, and it is read by
tooling as well as by people: the compiler decides what to compile from it, a
build system selects sources by it, and an editor picks a language mode from it.
Consistency is the whole value — a convention followed nine times out of ten
conveys nothing the tenth time, which is why the guideline puts consistency above
the specific letters chosen.

## Consequences

### Positive

- A file's role is legible from its name alone, without opening it.
- Build tooling and compiler agree on which files are code, because both key off
  the same two suffixes.
- Sharing a header with C stays possible without renaming anything.

### Negative

- The convention is a naming rule and nothing more: it does not stop a `.h` from
  containing definitions, which is why
  [0373](0373-sf-2-a-h-file-must-not-contain-object-definitions-or-non-inline-function-definitions.md)
  has to exist separately.
- Template-heavy code sometimes wants a third category — the file included at the
  end of a header to hold definitions — and this rule pushes that content back
  into the `.h`, making headers longer.
- Where a build selects sources by extension, a typo in a suffix produces a file
  that is compiled nowhere and fails at link time, with nothing pointing at the
  cause.

### Neutral

- The specific letters are not sacred. A project that had already standardised on
  `.hh` and `.cxx` keeps them, and this rule then means "be consistent" rather
  than "rename everything".
- A development environment may have strong opinions about suffixes of its own.
  That narrows which conventions are practically available; it does not change
  what the rule asks for once one is chosen.

## Enforcement

- Flag non-conventional file names.
- Check that `.h` and `.cpp` files, and their equivalents, follow the rules
  recorded alongside this one.

## Related

- [0373](0373-sf-2-a-h-file-must-not-contain-object-definitions-or-non-inline-function-definitions.md)
  — what a `.h` file is permitted to contain.
- [0374](0374-sf-3-use-h-files-for-all-declarations-used-in-multiple-source-files.md)
  — what belongs in a `.h` rather than being repeated.
- [0376](0376-sf-5-a-cpp-file-must-include-the-h-file-s-that-defines-its-interface.md)
  — how the two files are tied together.
- [0382](0382-sf-11-header-files-should-be-self-contained.md) — the property the
  `.h` half of the pair has to have.
