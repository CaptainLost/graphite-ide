# 0383. SF.12: Prefer the quoted form of #include for files relative to the including file and the angle bracket form everywhere else

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

The standard gives compilers flexibility in how they implement the two forms of
`#include` selected by the angle-bracket and quoted syntax. Vendors take
advantage of this and use different search algorithms and different ways of
specifying the include path.

Nevertheless, the guidance is to use the quoted form for files that exist at a
path relative to the file containing the `#include` — from within the same
component or project — and the angle-bracket form everywhere else, where
possible. This encourages being clear about the locality of a file relative to
the files that include it, and about the cases where the different search
algorithm is required. It makes it easy to see at a glance whether a header comes
from a local relative file, from the standard library, or from the alternate
search path — another library, or a common set of includes.

```cpp
// foo.cpp:
#include <string>                // From the standard library, requires the <> form
#include <some_library/common.h> // A file that is not locally relative, included
                                 // from another library; use the <> form
#include "foo.h"                 // A file locally relative to foo.cpp in the same
                                 // project, use the "" form
#include "foo_utils/utils.h"     // A file locally relative to foo.cpp in the same
                                 // project, use the "" form
#include <component_b/bar.h>     // A file in the same project located via a
                                 // search path, use the <> form
```

Failing to follow this results in errors that are difficult to diagnose, caused
by picking up the wrong file through an incorrectly specified scope. In the
typical case where the quoted search algorithm looks at the local relative path
first, using that form for a file that is not locally relative means that if a
file ever comes into existence at the local relative path — because the including
file was moved, for instance — it will be found ahead of the previous header, and
the set of includes will have changed in an unexpected way.

Library creators should put their headers in a folder and have clients include
them by relative path: `#include <some_library/common.h>`.

## Decision

Choose the include form by where the file is found, not by habit.

- Use the angle-bracket form for standard library headers, for third-party
  headers, and for any header located through a search path.
- Use the quoted form only for a header that sits at a path relative to the file
  doing the including.
- Do not use the quoted form as a way of saying "ours"; the distinction the rule
  draws is locality, not ownership.
- If you publish a library, put its headers in a folder and have clients include
  them by relative path, so that the include names a component regardless of
  which form is used.

## Rationale

The two forms differ in search order, not in meaning, and the quoted form
searches the local directory first. Using it for a header that is really found
via a search path leaves a latent trap: create or move a file so that a name
matches locally, and the include silently resolves somewhere else. Nothing warns,
and the symptom appears as a type mismatch far from the cause.

The readability argument is the everyday one. When the form tells you where a
header came from, an include block can be read at a glance.

## Consequences

### Positive

- The form of an include says where the header was found, which is the fact a
  reader wants.
- The silent-capture failure — a local file shadowing a search-path header —
  cannot happen.
- Moving a source file cannot change which headers its includes resolve to.

### Negative

- Existing code commonly uses the quoted form for headers that are really found
  through a search path. Correcting that is mechanical but touches most files,
  and on a single toolchain it buys nothing observable.
- The rule leans on the search algorithm being what it usually is, and the
  standard does not require that, so the guidance is a convention about vendor
  behaviour rather than something the language guarantees.
- Angle brackets for a project's own headers reads, to many people, as "not
  ours", so following the rule costs a signal that the quoted form was informally
  carrying.

### Neutral

- The rule is about which form, not about which directories are on the include
  path; that is a build decision.
- Putting headers under a component-named folder is good practice under either
  form; the rule decides the form, not whether the folder exists.

## Enforcement

A test should identify whether headers referenced with the quoted form could be
referenced with the angle-bracket form.

## Related

- [0382](0382-sf-11-header-files-should-be-self-contained.md) — where the
  relative-path caution is first raised.
- [0380](0380-sf-9-avoid-cyclic-dependencies-among-source-files.md) — both rules
  are about keeping the real dependency graph legible.
- [0372](0372-sf-1-use-a-cpp-suffix-for-code-files-and-h-for-interface-files-if-your-project-doesnt-already-follow-another-convention.md)
  — naming conventions for the files being included.
