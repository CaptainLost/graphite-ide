# 0375. SF.4: Include .h files before other declarations in a file

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

Minimise context dependencies and increase readability.

```cpp
#include <vector>
#include <algorithm>
#include <string>

// ... my code here ...
```

```cpp
// bad
#include <vector>

// ... my code here ...

#include <algorithm>
#include <string>
```

This applies to both `.h` and `.cpp` files.

There is an argument for insulating code from declarations and macros in header
files by including headers *after* the code being protected, as in the example
labelled bad. It does not hold up. It only works for one file at one level: use
the technique in a header that is itself included alongside other headers and the
vulnerability reappears. An implementation namespace protects against many
context dependencies more cheaply. And full protection with flexibility requires
modules, which a codebase composed of headers and translation units does not
have.

## Decision

Put every `#include` at the top of the file, before any declaration or
definition.

- Group the includes in one block at the head of the file, in both headers and
  implementation files.
- Do not move an include down the file to shield the code above it from a macro
  or a declaration; fix the offending header, or wrap the code in an
  implementation namespace.
- Do not place an include inside a namespace, a class, or a function body.
- Where a header must be included in a particular order for correctness, that is
  a defect in the header — see
  [0382](0382-sf-11-header-files-should-be-self-contained.md) — not a reason to
  scatter the includes.

## Rationale

The insulation argument is the only real case for the alternative, and it fails
on its own terms: it protects exactly one translation unit at exactly one level
of inclusion, and stops working the moment the file is itself included. Paying a
permanent readability cost for protection that evaporates on first reuse is a bad
trade.

The readability half matters independently. A reader who wants to know what a
file depends on should be able to find that out by looking at the top of it, and
only there.

## Consequences

### Positive

- A file's dependencies are visible in one place, at the top.
- No declaration in the file can change the meaning of a header included after
  it, because there are none.
- Include order becomes a property that can be checked mechanically.

### Negative

- It gives up the one genuine use of a trailing include — deliberately shielding
  code from a badly behaved third-party header that defines macros such as `min`
  and `max`. The remedy left is to suppress the macros at the point the offending
  header is configured, which pushes the problem onto that header's author.
- A long include block at the head of every file is itself noise, and hides which
  includes are actually needed.
- The rule constrains position, not content, so a file can obey it perfectly while
  including twenty headers it does not use.

### Neutral

- The rule fixes where includes go, not what order they are in among themselves;
  self-contained headers make that order irrelevant.
- The rule applies to headers and implementation files alike. Neither kind gets a
  dispensation, and a header is the place where a trailing include does the most
  damage, because its vulnerability is inherited by everything that includes it.

## Enforcement

Upstream records this as easy to enforce: a check can flag any `#include` that
appears after a declaration in the same file.

## Related

- [0382](0382-sf-11-header-files-should-be-self-contained.md) — why include order
  should not matter to correctness.
- [0381](0381-sf-10-avoid-dependencies-on-implicitly-included-names.md) — the
  other half of include discipline.
- [0376](0376-sf-5-a-cpp-file-must-include-the-h-file-s-that-defines-its-interface.md)
  — the one include a `.cpp` is obliged to have.
- [0378](0378-sf-7-dont-write-using-namespace-at-global-scope-in-a-header-file.md)
  — the other way a header can change the meaning of the code around it.
