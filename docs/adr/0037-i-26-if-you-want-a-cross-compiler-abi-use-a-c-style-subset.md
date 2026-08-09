# 0037. I.26: If you want a cross-compiler ABI, use a C-style subset

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

This project has no ABI. Core is a static library linked into one executable
built by one compiler; nothing is shipped for anyone else to link against. The
rule is transcribed for completeness and because its final Note is the one that
applies.

## Decision

Adopt I.26 as written, noting that its antecedent — "if you want a
cross-compiler ABI" — is false here. Transcribed in full:

> **Reason.** Different compilers implement different binary layouts for
> classes, exception handling, function names, and other implementation details.

**Exception.** Common ABIs are emerging on some platforms freeing you from the
more draconian restrictions.

**Note.** If you use a single compiler, you can use full C++ in interfaces. That
might require recompilation after an upgrade to a new compiler version.

**Enforcement.** (Not enforceable) It is difficult to reliably identify where an
interface forms part of an ABI.

## Rationale

Adopted vacuously, and recorded so that the reasoning is available if the
premise ever changes.

The Note is the operative sentence: a single compiler licenses full C++ in
interfaces, which is what Core does — `std::filesystem::path`,
`std::string_view` and `Result<T>` all cross the Core/Graphite boundary freely.
That is only safe because both sides are compiled together by the same MSVC,
per [0005](0005-target-c-20-with-headers-not-modules.md), and it is why
`staticruntime "on"` matters: everything shares one runtime.

The rule would become live if Core were ever shipped as a DLL or as a library
for someone else to link. At that point the boundary types above become the
problem, and this record would be superseded rather than amended.

## Consequences

### Positive

- Records why standard-library types are allowed to cross the Core/Graphite
  boundary, which would otherwise look careless to a reader used to library
  work.

### Negative

- The freedom is load-bearing and invisible. If Core is ever built separately —
  a different configuration, a different compiler version — the boundary types
  become a binary-compatibility hazard with no warning from the build.

### Neutral

- The Exception about emerging platform ABIs has no bearing here.

## Related

- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — the previous rule.
- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — the next
  rule, on the same subject.
- [0005](0005-target-c-20-with-headers-not-modules.md) — the single-compiler
  assumption this rests on.
