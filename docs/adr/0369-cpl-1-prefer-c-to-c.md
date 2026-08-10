# 0369. CPL.1: Prefer C++ to C

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CPL (C-style programming), transcribed 2026-08-10

## Context

C and C++ are closely related languages. Both originate in "Classic C" from
1978 and have evolved in ISO committees since; many attempts have been made to
keep them compatible, but neither is a subset of the other. Where the two
overlap, C++ provides better type checking and more notational support, offers
better support for high-level programming, and often generates faster code.

The clearest illustration is the treatment of `void*`:

```cpp
char ch = 7;
void* pv = &ch;
int* pi = pv;   // not C++
*pi = 999;      // overwrite sizeof(int) bytes near &ch
```

C converts to and from `void*` implicitly, so this compiles as C and writes
`sizeof(int)` bytes over memory that belongs to a single `char`. The rules for
that implicit conversion in C are subtle and unenforced; in particular, this
example violates a rule against converting to a type with stricter alignment.
C++ rejects the conversion outright, so the same text does not compile, and the
bug never reaches a run.

## Decision

Write C++, not C, wherever the choice is available.

- Compile every source file as C++. Do not add a translation unit whose build
  mode is C.
- Do not rely on the implicit `void*` conversions that C permits; a conversion
  that is genuinely required is written as a named cast and is visible.
- Prefer the C++ facility to its C counterpart where both exist: `new` and
  container types over `malloc`, `std::string` and `std::string_view` over
  `char*` manipulation, templates and overloads over macros and `void*`-based
  genericity.
- Where C code must be used — a third-party C library, or a system interface
  that is only offered as C — it stays on its own side of the boundary, and the
  code calling it is C++.

## Rationale

This is not a stylistic preference between two comparable languages: on the
overlap, C++ diagnoses at compile time a class of error that C accepts
silently. The `void*` example costs nothing to write correctly in C++ because
the compiler will not let it be written incorrectly, and there is no
corresponding cost paid to gain that.

The efficiency argument that historically favoured C has reversed. Better type
information gives the optimiser more to work with, so choosing C for speed is a
claim that would need measuring before it could be believed.

## Consequences

### Positive

- Conversions that C would perform silently become compiler diagnostics, and
  where warnings are treated as errors they stop the build.
- The whole type system, RAII, and the standard library remain available; most
  of the other guideline rules assume them.
- Overload resolution, templates and stronger `const` checking make intent
  expressible without macros or untyped pointers.

### Negative

- C++ is a far larger language, and its extra expressiveness is also extra
  surface to get wrong; the guidelines exist precisely because that surface
  needs discipline.
- Compilation is slower and diagnostics are longer than the C equivalent, most
  noticeably in template-heavy code.
- The rule does not help where C is not a choice: an operating-system header, a
  C library's public interface, or an ABI boundary is still C, and the
  restrictions that come with it apply there regardless.
- "Prefer" leaves the boundary case undecided. Whether a small, dependency-free
  utility is worth writing in the common subset so it can be shared with a C
  build is a judgement this rule does not make.

### Neutral

- A file with a `.c` extension is not automatically C code; what matters is the
  mode the compiler is invoked in.
- Nothing here forbids using a C library. It constrains only the language the
  calling code is written in.

## Enforcement

- Use a C++ compiler.

## Related

- [0370](0370-cpl-2-if-you-must-use-c-use-the-common-subset-of-c-and-c-and-compile-the-c-code-as-c.md)
  — what to do when C cannot be avoided.
- [0371](0371-cpl-3-if-you-must-use-c-for-interfaces-use-c-in-the-calling-code-using-such-interfaces.md)
  — the calling side of a C interface.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — the
  type safety this rule is a concrete instance of.
- [0205](0205-es-48-avoid-casts.md) — the `void*` round trip is the cast this
  record is about.
