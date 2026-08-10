# 0382. SF.11: Header files should be self-contained

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

Usability. Headers should be simple to use and should work when included on their
own. A header should encapsulate the functionality it provides, and clients of a
header should not have to manage that header's dependencies.

```cpp
#include "helpers.h"
// helpers.h depends on std::string and includes <string>
```

Failing to follow this results in errors that are difficult to diagnose for
clients of a header: the compiler reports an undeclared name inside a file the
client did not write, and the fix is an include the client has no reason to
suspect is missing.

A header should include all of its dependencies. Be careful about using relative
paths, because C++ implementations diverge on their meaning.

## Decision

Every header compiles on its own, included first, into an empty translation unit.

- Include, in the header, every header it needs for its own declarations to be
  well formed.
- Forward-declare a type where a declaration suffices — a pointer, a reference,
  or a return type not used by value — and include the full definition where it
  does not.
- Do not depend on the includer having included something first, and do not
  document such a requirement as a workaround.
- Be careful with relative include paths, whose meaning varies between
  implementations; see
  [0383](0383-sf-12-prefer-the-quoted-form-of-include-for-files-relative-to-the-including-file-and-the-angle-bracket-form-everywhere-else.md).

## Rationale

Self-containment is what makes a header an interface rather than a fragment. A
header that only compiles in the right context has an undeclared precondition on
its includer, and that precondition is invisible, unversioned, and discovered by
whoever includes the header somewhere new.

It is also the one property in this section that can be tested outright: compile
a `.cpp` that contains nothing but the include. Nothing else in header hygiene has
so direct a check, which is why this rule is worth enforcing even where the others
are left to judgement.

## Consequences

### Positive

- A header can be included anywhere, in any order, which makes include order a
  matter of style rather than of correctness.
- The failure mode — a diagnostic inside someone else's header — disappears.
- The property is mechanically testable, one translation unit per header.

### Negative

- Self-contained headers include more, so compile times rise; every dependency is
  paid for by every includer, whether or not it uses the part that needed it.
- It pushes towards forward declarations to keep the includes down, and those are
  hand-maintained copies of a declaration, with the drift risk
  [0374](0374-sf-3-use-h-files-for-all-declarations-used-in-multiple-source-files.md)
  warns about.
- A per-header compilation test costs build time and has to be generated and
  maintained; without one, the rule holds only as long as every header happens
  to be included first somewhere.

### Neutral

- The rule says a header includes its dependencies; it does not say a header must
  be small, and the two goals frequently pull apart.
- Third-party headers are outside the rule's reach; they are self-contained or
  they are not, and that is their author's decision rather than their user's.

## Enforcement

A test should verify that the header file itself compiles, or that a `.cpp` file
which only includes the header compiles.

## Related

- [0381](0381-sf-10-avoid-dependencies-on-implicitly-included-names.md) — the
  same discipline stated from the includer's side.
- [0376](0376-sf-5-a-cpp-file-must-include-the-h-file-s-that-defines-its-interface.md)
  — including the header first in its own `.cpp` is a partial test of this rule.
- [0379](0379-sf-8-use-include-guards-for-all-h-files.md) — a header that can be
  included anywhere must survive being included twice.
- [0383](0383-sf-12-prefer-the-quoted-form-of-include-for-files-relative-to-the-including-file-and-the-angle-bracket-form-everywhere-else.md)
  — the relative-path caution this rule raises.
