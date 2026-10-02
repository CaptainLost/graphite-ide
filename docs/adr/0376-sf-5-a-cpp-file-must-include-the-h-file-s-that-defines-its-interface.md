# 0376. SF.5: A .cpp file must include the .h file(s) that defines its interface

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

Including the header enables the compiler to do an early consistency check.

```cpp
// bad
// foo.h:
void foo(int);
int bar(long);
int foobar(int);

// foo.cpp:
void foo(int) { /* ... */ }
int bar(double) { /* ... */ }
double foobar(int);
```

The errors are not caught until link time, and only for a program that actually
calls `bar` or `foobar`. Include the header and one of them is caught
immediately:

```cpp
// foo.h:
void foo(int);
int bar(long);
int foobar(int);

// foo.cpp:
#include <foo.h>

void foo(int) { /* ... */ }
int bar(double) { /* ... */ }
double foobar(int);   // error: wrong return type
```

The return-type error for `foobar` is now reported when `foo.cpp` is compiled: a
declaration cannot differ from another declaration of the same function in return
type alone. The argument-type error for `bar` still cannot be caught until link
time, because overloading makes `bar(double)` a legitimate second function rather
than a mistake. Systematic use of `.h` files increases the likelihood that the
programmer notices it earlier all the same.

## Decision

Every `.cpp` includes the header that declares what it defines.

- Include it whether or not the file needs anything from it to compile; the
  purpose is the check, not the declarations.
- Include it first, or near enough to first, so that the header is also proved to
  be self-contained as a side effect — see
  [0382](0382-sf-11-header-files-should-be-self-contained.md).
- Where a `.cpp` implements more than one header, include all of them.
- Do not silence a mismatch by editing the definition's signature to match a
  guess; change the declaration and the definition together, in one edit.

## Rationale

This is the cheapest consistency check available in a header-based build: one
line, no tooling, and it converts a class of link-time error into a compile-time
error at the exact point the mistake was made. What it does not catch is worth
knowing too — overload resolution means a wrong parameter type produces a second
function rather than a diagnostic, so the rule narrows the window rather than
closing it.

## Consequences

### Positive

- Return-type and declaration mismatches are caught while compiling the file that
  contains them.
- Including the header first also proves the header compiles standalone, which is
  the enforcement mechanism suggested for
  [0382](0382-sf-11-header-files-should-be-self-contained.md).
- The `.cpp` cannot drift into implementing something the header does not
  advertise without that being visible.

### Negative

- It does not catch a mismatch in parameter types, which is the more common
  mistake, because overloading makes such a definition legal.
- It couples every implementation file to its header's own includes, so a change
  to the header recompiles the implementation even when nothing it uses changed.
- A `.cpp` that has no header — a translation unit holding only internal
  definitions, or the one defining `main` — is outside the rule, and the rule
  says nothing about how those are checked.

### Neutral

- The good and bad versions of the example differ by one line. The rule changes
  nothing about what the implementation file contains, only about what the
  compiler is shown while compiling it.
- The rule is about the interface header specifically, not about including every
  header a file uses; that is
  [0381](0381-sf-10-avoid-dependencies-on-implicitly-included-names.md).

## Related

- [0374](0374-sf-3-use-h-files-for-all-declarations-used-in-multiple-source-files.md)
  — the declaration this rule checks the definition against.
- [0375](0375-sf-4-include-h-files-before-other-declarations-in-a-file.md) —
  where the include goes.
- [0382](0382-sf-11-header-files-should-be-self-contained.md) — the property this
  rule incidentally proves.
- [0372](0372-sf-1-use-a-cpp-suffix-for-code-files-and-h-for-interface-files-if-your-project-doesnt-already-follow-another-convention.md)
  — the file pairing this rule assumes.
