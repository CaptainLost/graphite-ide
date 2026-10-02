# 0389. SL.3: Do not add non-standard entities to namespace std

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

Adding to `std` might change the meaning of otherwise standards-conforming code,
and additions to `std` might clash with future versions of the standard.

Both halves are ways of saying that `std` is not yours. Name lookup on standard
types finds names in `std` by argument-dependent lookup, so a function or
operator injected there participates in overload resolution in code that never
asked for it, in translation units that merely include a standard header. The
effect is not local to the file that did the injecting.

The second half is a slower failure. A name added to `std` today is a name the
committee may standardise tomorrow with different semantics, and the code then
either fails to compile or silently changes behaviour on a compiler upgrade.
Nothing about the addition marks it as the thing that broke.

Upstream supplies no example for this rule.

## Decision

Leave namespace `std` alone.

- Do not declare new functions, classes, variables, or overloads in namespace
  `std`.
- Put helpers for your own types in the namespace of those types, where
  argument-dependent lookup finds them, rather than next to the standard type
  they happen to be used with.
- Where the standard explicitly sanctions a specialisation for a
  program-defined type — `std::hash` is the usual case — provide it as a
  specialisation, not as a new declaration, and only for a type you own.
- Prefer the customisation route the library offers over the one that requires
  opening `std` at all: a comparator or hash object passed as a template
  argument needs no specialisation.

## Rationale

The damage here is unbounded in scope. A name in your own namespace can only
affect code that names that namespace; a name in `std` can affect any
translation unit that includes any standard header, through overload resolution
paths nobody wrote down. That asymmetry is what makes an otherwise convenient
shortcut not worth taking.

The forward-compatibility argument is the one that bites in practice: the
addition works, ships, and then breaks years later on a toolchain upgrade, with
a diagnostic that points at standard library headers rather than at the
addition.

## Consequences

### Positive

- Standard headers keep the meaning the standard gives them, in every
  translation unit.
- Compiler and standard library upgrades cannot collide with a local addition.
- Helper functions live next to the types they serve, which is where a reader
  looks for them.

### Negative

- The sanctioned specialisation route is more ceremonious than simply writing a
  free function, particularly for `std::hash`, which needs a full struct
  template specialisation to add one function.
- Some genuinely useful integrations — streaming a third-party type, hashing a
  type you do not own — have no clean route at all once this rule is followed,
  and the workaround is a wrapper type.
- The rule is not mechanically enforced by the language: the compiler accepts
  most additions to `std` happily, and the standard's prohibition is undefined
  behaviour rather than a diagnostic.

### Neutral

- Specialising a standard template for a program-defined type is permitted by
  the standard and is not what this rule prohibits.
- The rule says nothing about macros, which can break standard headers just as
  thoroughly by other means.

## Enforcement

- Checking this automatically is possible but messy, and likely to cause
  problems with platform-specific standard library implementations, which
  legitimately add to `std` themselves.

## Related

- [0044](0044-c-5-place-helper-functions-in-the-same-namespace-as-the-class-they-support.md)
  — where the helper belongs instead.
- [0130](0130-c-165-use-using-for-customization-points.md) — the sanctioned way
  to hook into a standard-library customisation point.
- [0008](0008-p-2-write-in-iso-standard-c.md) — relying on standard-conforming
  behaviour is only meaningful if nothing has redefined the standard names.
- [0390](0390-sl-4-use-the-standard-library-in-a-type-safe-manner.md) — the
  umbrella rule for using the library as intended.
