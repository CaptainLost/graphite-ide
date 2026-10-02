# 0381. SF.10: Avoid dependencies on implicitly #included names

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

Avoid surprises. Avoid having to change includes when an included header changes.
Avoid accidentally becoming dependent on implementation details and on logically
separate entities that happen to be included by a header.

```cpp
// bad
#include <iostream>

using namespace std;

void use()
{
    string s;
    cin >> s;               // fine
    getline(cin, s);        // error: getline() not defined
    if (s == "surprise") {  // error == not defined
        // ...
    }
}
```

`<iostream>` exposes the definition of `std::string`, but it is not required to
do so by transitively including the whole of `<string>`. The result is the
popular beginner question "why doesn't `getline(cin, s)` work?", and the
occasional "strings cannot be compared with `==`". The solution is to include
`<string>` explicitly:

```cpp
// good
#include <iostream>
#include <string>

using namespace std;

void use()
{
    string s;
    cin >> s;               // fine
    getline(cin, s);        // fine
    if (s == "surprise") {  // fine
        // ...
    }
}
```

Some headers exist exactly to collect a set of consistent declarations from a
variety of headers:

```cpp
// basic_std_lib.h:
#include <string>
#include <map>
#include <iostream>
#include <random>
#include <vector>
```

A user can then obtain that set with a single `#include "basic_std_lib.h"`. The
rule against implicit inclusion is not meant to prevent such deliberate
aggregation.

## Decision

Include what you use. Do not rely on a name arriving through some other header.

- For every type, function, or macro a file names, include the header that the
  standard or the component documents as providing it.
- Do not remove an include because the file still compiles without it; it
  compiles because something else supplied the name, and that is exactly the
  dependency being forbidden.
- Treat a header that is included only to obtain a transitive dependency as a
  bug in the including file, not as a shortcut.
- A deliberate aggregating header is permitted, but including one is not a
  licence to omit an include: a file that uses `std::vector` includes `<vector>`
  whether or not something else would have supplied it.

## Rationale

An implicit include is a dependency on someone else's implementation detail, and
it breaks when that detail changes — on a standard library upgrade, or when a
header stops needing something it used to include. The breakage lands in a file
that was not edited, which makes it expensive to diagnose out of all proportion
to the one line that would have prevented it.

The exception for deliberate aggregation shows what the rule is really about. A
header that exists to collect declarations is promising them, and depending on
that promise is fine; what is forbidden is depending on an inclusion nobody ever
undertook to keep. The distinction is intent, not transitivity — which is also
why no check can draw it without knowing which names a header means to export.

## Consequences

### Positive

- A file's dependencies are stated rather than inherited, so it can be moved,
  reused, or compiled on its own.
- A standard library or third-party upgrade that changes transitive includes does
  not break unrelated files.
- Reading the include block tells you roughly what the file does.

### Negative

- The discipline is entirely manual where headers rather than modules carry the
  interfaces, since nothing distinguishes an exported name from one that leaked
  in through an implementation detail.
- An aggregating header conceals breaches of the rule for as long as it is
  included: the missing includes still compile, so omissions accumulate quietly
  and would all surface at once if it were ever trimmed. "Does it still
  compile?" is therefore not a check on this rule.
- Explicit includes make the include block longer and add real compile time,
  which is in tension with keeping the build fast — the same tension an
  aggregating header exists to relieve.

### Neutral

- Deliberate aggregation is explicitly permitted, so an aggregating header does
  not violate this rule; the files that lean on it do.
- The rule concerns which headers a file includes, not where the includes go;
  that is
  [0375](0375-sf-4-include-h-files-before-other-declarations-in-a-file.md).

## Enforcement

Enforcement would require knowing what in a header is meant to be exported to
users and what is there to enable the implementation. Upstream records that no
really good solution is possible without modules.

## Related

- [0382](0382-sf-11-header-files-should-be-self-contained.md) — the same
  discipline stated from the header's side.
- [0375](0375-sf-4-include-h-files-before-other-declarations-in-a-file.md) —
  where those includes belong.
- [0380](0380-sf-9-avoid-cyclic-dependencies-among-source-files.md) — explicit
  includes make the real dependency graph visible.
