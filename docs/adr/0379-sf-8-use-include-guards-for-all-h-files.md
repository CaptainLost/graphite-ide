# 0379. SF.8: Use #include guards for all .h files

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

The reason is to avoid files being included several times. A header included
twice in one translation unit redefines every class it contains, which is an
error, and the second inclusion is usually indirect and therefore not visible at
the point it happens.

In order to avoid guard collisions, do not just name the guard after the
filename. Be sure to include a key and a good differentiator as well, such as the
name of the library or component the header file is part of.

```cpp
// file foobar.h:
#ifndef LIBRARY_FOOBAR_H
#define LIBRARY_FOOBAR_H

// ... declarations ...

#endif // LIBRARY_FOOBAR_H
```

Some implementations offer vendor extensions such as `#pragma once` as an
alternative to include guards. It is not standard and it is not portable. It
injects the hosting machine's filesystem semantics into the program, in addition
to locking the code down to a vendor. The recommendation is to write in ISO C++,
per [0008](0008-p-2-write-in-iso-standard-c.md).

## Decision

Give every `.h` file an include guard, without exception.

- Open the header with `#ifndef GUARD` and `#define GUARD`, and close it with
  `#endif` at the very end of the file.
- Build the guard name from a component or library differentiator as well as the
  filename — `LIBRARY_FOOBAR_H`, not `FOOBAR_H` — so that two components with a
  file of the same name do not silently blank one another out.
- Put nothing outside the guarded region, so that a second inclusion really is
  free.
- Prefer the standard guard to a vendor extension such as `#pragma once`, whose
  notion of when two paths name the same file is defined by the implementation
  rather than by the language.

## Rationale

The guard is what makes a header includable from anywhere without its includer
having to know what else has already been included, and that property is
presupposed by every other rule in this section. Without it, correctness depends
on the sequence and the multiplicity of includes across a whole translation unit,
which is not something any one file can reason about.

The naming half of the rule is the part most often skipped and the one with the
nastiest failure. A guard named after the filename alone collides with the same
filename in another library; the second header then expands to nothing, and the
diagnostic is a missing declaration in a file that plainly declares it.

## Consequences

### Positive

- A header can be included repeatedly, directly or transitively, with no effect
  after the first time.
- The construct is ISO C++, so it behaves the same under every compiler.
- A differentiated guard name makes collisions between same-named headers in
  different components impossible.

### Negative

- The guard is a name that must be unique across the entire program yet is
  written by hand, and it is copied along with the file whenever a header is
  duplicated as a starting point — the most common way a collision is created.
- Renaming or moving a header leaves the old guard name behind, correct but
  misleading, and nothing in the build notices.
- Three lines of boilerplate per header, and a mismatched or misplaced `#endif`
  silently truncates the file's contents rather than failing loudly.
- It forgoes `#pragma once`, which is accepted by every compiler in wide use, is
  a single line, and cannot be misspelled into a collision. The rule trades that
  convenience for a guarantee from the standard rather than from a vendor.

### Neutral

- Guards are invisible above the preprocessor; nothing about an interface changes
  when the mechanism does.
- A guard makes a cyclic include terminate. It does not make it correct, which is
  why [0380](0380-sf-9-avoid-cyclic-dependencies-among-source-files.md) insists on
  eliminating cycles rather than relying on guards to absorb them.

## Enforcement

- Flag `.h` files without include guards.

## Related

- [0008](0008-p-2-write-in-iso-standard-c.md) — the ISO-C++ rule that rules out
  `#pragma once` as the mechanism.
- [0380](0380-sf-9-avoid-cyclic-dependencies-among-source-files.md) — what guards
  do not fix.
- [0382](0382-sf-11-header-files-should-be-self-contained.md) — a self-contained
  header is one that can be included anywhere, which assumes it is guarded.
- [0373](0373-sf-2-a-h-file-must-not-contain-object-definitions-or-non-inline-function-definitions.md)
  — a guard protects one translation unit from double inclusion, not the program
  from a duplicated definition.
