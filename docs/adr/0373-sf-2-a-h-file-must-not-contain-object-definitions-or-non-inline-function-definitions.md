# 0373. SF.2: A .h file must not contain object definitions or non-inline function definitions

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

Including entities that are subject to the one-definition rule leads to linkage
errors.

```cpp
// file.h:
namespace Foo {
    int x = 7;
    int xx() { return x + x; }
}

// file1.cpp:
#include <file.h>
// ... more ...

// file2.cpp:
#include <file.h>
// ... more ...
```

Linking `file1.cpp` and `file2.cpp` gives two linker errors. Nothing is wrong
with either translation unit on its own; the error appears only once both are
put together, which is the latest and least informative moment for it to appear.

The rule has an equivalent positive formulation. A `.h` file must contain only:

- `#include`s of other `.h` files, possibly with include guards;
- templates;
- class definitions;
- function declarations;
- `extern` declarations;
- `inline` function definitions;
- `constexpr` definitions;
- `const` definitions;
- `using` alias definitions.

Everything on that list either declares without defining, or defines something
the language explicitly permits to be defined identically in every translation
unit. An object definition and a non-`inline` function definition are neither.

## Decision

Put no definition in a `.h` file that the one-definition rule requires to appear
exactly once in the program.

- Declare a free function in the header and define it in the corresponding
  `.cpp`, unless it is `inline`, `constexpr`, or a template.
- Declare a namespace-scope variable `extern` in the header and define it in one
  `.cpp`; better still, do not have one — see
  [0021](0021-i-2-avoid-non-const-global-variables.md).
- Mark a function `inline` deliberately when its definition genuinely belongs in
  the header, rather than relying on it being a member defined inside a class.
- Keep the header to the positive list above: includes, templates, class
  definitions, declarations, `inline` and `constexpr` and `const` definitions,
  and `using` aliases.

## Rationale

The sharp part of this rule is when the failure arrives. A definition in a header
is not a compile error in any single translation unit — every one of them is
individually valid — so the diagnosis is deferred to the linker, which reports a
duplicate symbol with no line number worth reading and no indication of which
header caused it. Restricting the header to the positive list makes the class of
error impossible rather than merely detectable.

## Consequences

### Positive

- Duplicate-symbol link errors from headers cannot occur.
- A header stays an interface, so reading it tells you what exists without
  telling you how it works.
- Changing an implementation recompiles one translation unit rather than every
  includer.

### Negative

- Splitting a small function across two files is real ceremony for something that
  would have been three lines in the header, and the temptation to write it
  `inline` instead is constant.
- The rule interacts awkwardly with templates: a template definition must live in
  the header, so the "interface only" reading of a header is not true of
  template-heavy code, and the header grows into the implementation anyway.
- `inline` is an escape hatch wide enough to swallow the rule. A header full of
  `inline` definitions satisfies the letter of it and gives up most of the
  compile-time benefit.

### Neutral

- `constexpr` and `const` namespace-scope definitions are permitted, so constants
  do belong in headers.
- C++17 `inline` variables extend the permitted list in practice; they are the
  variable equivalent of an `inline` function and carry the same trade-off.

## Enforcement

Check the positive list above: a `.h` file should contain nothing outside it.

## Related

- [0372](0372-sf-1-use-a-cpp-suffix-for-code-files-and-h-for-interface-files-if-your-project-doesnt-already-follow-another-convention.md)
  — which file is a header in the first place.
- [0374](0374-sf-3-use-h-files-for-all-declarations-used-in-multiple-source-files.md)
  — the complementary rule about what must go into a header.
- [0385](0385-sf-21-dont-use-an-unnamed-anonymous-namespace-in-a-header.md) —
  another thing a header must not contain, for a related reason.
- [0021](0021-i-2-avoid-non-const-global-variables.md) — why the object
  definition this rule bans is usually a mistake regardless of where it lives.
