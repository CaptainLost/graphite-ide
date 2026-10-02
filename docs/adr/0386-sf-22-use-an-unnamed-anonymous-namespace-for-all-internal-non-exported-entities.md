# 0386. SF.22: Use an unnamed (anonymous) namespace for all internal/non-exported entities

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

Nothing external can depend on an entity in a nested unnamed namespace. Consider
putting every definition in an implementation source file in an unnamed namespace
unless it is defining an external or exported entity.

```cpp
// bad
static int f();
int g();
static bool h();
int k();
```

```cpp
// good
namespace {
    int f();
    bool h();
}

int g();
int k();
```

The two versions have the same linkage. What differs is that the good one groups
the internal entities and says once, structurally, that `f` and `h` are private to
this translation unit, instead of repeating `static` on each declaration and
leaving the reader to work out the shape of the file's public surface by scanning
for a keyword.

An API class and its members cannot live in an unnamed namespace. But any helper
class or function defined in an implementation source file should be at unnamed
namespace scope.

## Decision

In a `.cpp`, put everything that is not part of the published interface in an
unnamed namespace.

- Wrap helper functions, helper classes, file-local constants and file-local
  types in a single unnamed namespace near the top of the implementation file.
- Prefer the unnamed namespace to `static` at namespace scope; it covers types as
  well as functions and objects, and it groups rather than annotates.
- Leave outside the unnamed namespace exactly what the header declares — the
  definitions the rest of the program links against.
- Never do this in a header; see
  [0385](0385-sf-21-dont-use-an-unnamed-anonymous-namespace-in-a-header.md).

## Rationale

The value is a guarantee rather than a convention: an entity with internal linkage
cannot be named, called, or linked against from anywhere else, so it can be
renamed, changed, or deleted without consulting the rest of the program. That is
a stronger statement than "nobody currently uses it", and it is the only such
guarantee available at file granularity in a header-based build.

Grouping is the secondary benefit and the one read most often. A file whose
internal entities sit in one block has a public surface you can see, which is what
[0374](0374-sf-3-use-h-files-for-all-declarations-used-in-multiple-source-files.md)
assumes when it says only shared declarations belong in a header.

## Consequences

### Positive

- Internal helpers are unreachable from other translation units, so they can be
  changed freely.
- A file's public surface is what remains outside the block, visible at a glance.
- The linker has less to do and the optimiser more scope, since an
  internal-linkage function has a known and complete set of callers.

### Negative

- Internal linkage puts an entity out of reach of a test translation unit as well
  as of the rest of the program, so a helper worth testing directly has to be
  promoted to a header, and the rule offers no middle ground.
- The moment a second `.cpp` needs the helper, the unnamed namespace has to be
  undone — a real edit, in the file that was supposed to be insulated from such
  changes.
- Wrapping the file's contents adds a level of indentation and a block whose
  extent is easy to lose track of in a long file, and a definition accidentally
  added inside it silently changes linkage rather than failing.

### Neutral

- `static` at namespace scope is not wrong, merely weaker; this rule prefers the
  namespace without deprecating the keyword.
- Whether a given helper is internal is a design question the rule does not
  answer; it only says where the answer is written down.

## Related

- [0385](0385-sf-21-dont-use-an-unnamed-anonymous-namespace-in-a-header.md) — the
  same construct in the place it must not appear.
- [0374](0374-sf-3-use-h-files-for-all-declarations-used-in-multiple-source-files.md)
  — the complementary rule for entities that are shared.
- [0373](0373-sf-2-a-h-file-must-not-contain-object-definitions-or-non-inline-function-definitions.md)
  — the `.h`/`.cpp` split this rule sharpens.
- [0384](0384-sf-20-use-namespaces-to-express-logical-structure.md) — the named
  namespaces this one sits inside.
