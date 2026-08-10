# 0378. SF.7: Don't write using namespace at global scope in a header file

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

Writing `using namespace` at global scope in a header takes away an includer's
ability to effectively disambiguate and to use alternatives. It also makes
included headers order-dependent, as they might have different meaning when
included in different orders.

```cpp
// bad.h
#include <iostream>

using namespace std; // bad
```

```cpp
// user.cpp
#include "bad.h"

bool copy(/*... some parameters ...*/);    // some function that happens to be
                                           // named copy

int main()
{
    copy(/*...*/);    // now overloads local ::copy and std::copy, could be
                      // ambiguous
}
```

The author of `user.cpp` did nothing wrong and has no local remedy: the names
were injected into the global namespace by a file they merely included, and the
only way to get out of it is to stop including that header.

There is one exception: `using namespace std::literals;`. It is necessary in
order to use string literals in header files, and because users are required to
name their own user-defined literals `operator""_x`, they will not collide with
the standard library.

## Decision

Do not write a `using namespace` directive at global scope in a header.

- Qualify names fully in headers, or introduce a namespace alias, which affects
  only the name it defines.
- If a directive is genuinely needed for readability, put it inside the
  narrowest enclosing scope — a function body, or an inline definition — never at
  file scope.
- `using namespace std::literals;` in a header is permitted, because
  user-defined literal names are reserved in a way that makes collision
  impossible.
- Move any directive that exists for the benefit of the implementation into the
  `.cpp`, where
  [0377](0377-sf-6-use-using-namespace-directives-for-transition-for-foundation-libraries-such-as-std-or-within-a-local-scope-only.md)
  governs it.

## Rationale

The decisive point is who pays. A directive in a `.cpp` is a choice made by the
person who bears its consequences; a directive in a header is a choice imposed on
everyone who includes it, transitively, without their knowledge and with no way
to opt out. That asymmetry is why this case is excluded outright rather than being
one more judgement call.

The order-dependence is the second-order damage: once two headers can each change
what names are visible, the meaning of a translation unit depends on the sequence
in which its includes appear, and include order stops being a matter of taste.

## Consequences

### Positive

- A header cannot change the meaning of code that includes it.
- Include order stays irrelevant to correctness, which is what
  [0382](0382-sf-11-header-files-should-be-self-contained.md) assumes.
- An includer keeps the ability to define its own `copy`, `swap` or `count`.

### Negative

- Headers become more verbose: every standard-library name carries `std::`, and
  every name from a nested namespace carries its full path.
- The rule is easy to violate at one remove: include a third-party header that
  breaks it and the injected names propagate onwards through every header that
  includes it, with nothing at the eventual clash to indicate the source.
- The `std::literals` exception is a genuine hole. It is well argued, but it means
  "no directives in headers" is not literally true and a checker has to know
  about the special case.

### Neutral

- The rule concerns global scope specifically; a directive inside a function
  defined in a header is bounded by that function and is not covered.
- Nothing here restricts a `using` declaration that names a single entity, which
  is often the right tool.

## Enforcement

- Flag `using namespace` at global scope in a header file.

## Related

- [0377](0377-sf-6-use-using-namespace-directives-for-transition-for-foundation-libraries-such-as-std-or-within-a-local-scope-only.md)
  — where the directive is permitted.
- [0373](0373-sf-2-a-h-file-must-not-contain-object-definitions-or-non-inline-function-definitions.md)
  — the other rule about what a header must not contain.
- [0382](0382-sf-11-header-files-should-be-self-contained.md) — order-dependence
  is exactly what a self-contained header rules out.
- [0385](0385-sf-21-dont-use-an-unnamed-anonymous-namespace-in-a-header.md) — a
  second namespace construct that has no business in a header.
