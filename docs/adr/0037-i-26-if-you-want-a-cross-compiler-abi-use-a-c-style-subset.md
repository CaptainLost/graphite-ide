# 0037. I.26: If you want a cross-compiler ABI, use a C-style subset

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Different compilers implement different binary layouts for classes, exception
handling, function names and other implementation details. An interface that
crosses a compiler boundary and uses any of them is not portable, however
standard the source is.

Common ABIs are emerging on some platforms, which frees one from the more
draconian restrictions.

If a single compiler is used, full C++ can be used in interfaces. That might
require recompilation after an upgrade to a new compiler version.

## Decision

Where an interface must be usable from code built by a different compiler, or a
different version of the same one, restrict it to a C-style subset.

- Such a boundary uses `extern "C"` functions, C-layout types, and no
  exceptions, no class layouts, no templates, and no standard-library types in
  its signatures.
- Where a boundary is internal, and everything on both sides is built together,
  use full C++; the cost of a rebuild after a toolchain upgrade is accepted.
- Identify which interfaces, if any, are ABI boundaries, and say so where they
  are declared. An interface that has become one by accident is the failure this
  rule guards against.

## Rationale

The distinction that matters is not technical but organisational: does anything
on the other side of this interface get compiled separately from this side? For
most interfaces the answer is no, and there the restriction buys nothing and
costs the entire expressiveness of the language.

Stating the boundary explicitly is the useful half. The expensive mistake is a
plugin or export interface that was written in full C++ because nobody had
decided it was an ABI, and could not be changed afterwards.

## Consequences

### Positive

- An interface deliberately restricted to a C-style subset works across
  compilers and versions.
- Being forced to name ABI boundaries prevents one from appearing by accident.
- Internal interfaces keep full C++, because they are not boundaries.

### Negative

- A C-style interface loses everything the type system was giving: no RAII, no
  templates, no exceptions, ownership by convention. Every other record in this
  section is weakened at exactly that boundary.
- The wrapping and unwrapping either side of it is code with no purpose except
  the boundary.
- Whether an interface is an ABI boundary is a judgement that changes over the
  life of a project, and getting it wrong is expensive in both directions.

### Neutral

- Where a common platform ABI exists, the restrictions can be relaxed; that is a
  per-platform decision.
- The single-compiler case is explicitly permitted to use full C++, at the cost
  of recompilation on upgrade.

## Enforcement

(Not enforceable.) It is difficult to reliably identify where an interface forms
part of an ABI.

## Related

- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — the
  same problem for a single compiler, where only recompilation is at stake.
- [0008](0008-p-2-write-in-iso-standard-c.md) — standard C++ is not the same as
  a stable ABI, and this record is why.
- [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  — the ABI caveat on passing `span` across a boundary.
