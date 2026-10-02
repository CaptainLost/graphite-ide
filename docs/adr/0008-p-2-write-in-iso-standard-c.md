# 0008. P.2: Write in ISO Standard C++

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

These guidelines are a set of rules for writing ISO Standard C++. That is the
dialect they assume throughout.

There are environments where extensions are necessary — to access system
resources, for one. Extensions often do not have rigorously defined semantics.
Even extensions that are common and implemented by multiple compilers might have
slightly different behaviour, and different edge-case behaviour, as a direct
result of not having a rigorous standard definition. With sufficient use of any
such extension, expected portability will be impacted.

Using valid ISO C++ does not by itself guarantee portability, let alone
correctness. Undefined behaviour — an undefined order of evaluation, for
instance — remains undefined in perfectly standard code, and constructs with
implementation-defined meaning, such as `sizeof(int)`, remain
implementation-defined.

There are also environments where restrictions on the use of standard C++
language or library features are necessary — avoiding dynamic memory allocation,
as required by aircraft control software standards, for example. Such
restrictions are a customisation of these guidelines to a specific environment,
not an exception to them.

## Decision

Write ISO Standard C++.

Where a platform extension is genuinely necessary, localise it and control its
use:

- Confine the extension to the smallest possible region of code — ideally to a
  single translation unit behind an interface that is itself standard C++, so
  that the extension can be turned off or compiled away on systems that do not
  support it.
- Do not let an extension's types or macros appear in a header that
  platform-independent code includes.
- Do not depend on undefined behaviour, and treat implementation-defined
  constructs as things to be pinned down explicitly rather than assumed.

Compile with a set of options that do not accept extensions, so that a
non-standard construct is a build failure rather than a portability debt
discovered later.

## Rationale

Portability is not the only reason, and not the main one. A standard construct
has a definition that can be argued about, looked up, and relied on across
compiler versions; an extension has whatever this compiler does this year. The
cost of finding out that two compilers disagree about an extension's edge cases
is paid at the worst possible time — during a port, or after an upgrade — and
is paid by whoever is least equipped to understand why the code was written that
way.

Localising rather than banning is the practical half of the rule. Some things
genuinely cannot be done in standard C++, and pretending otherwise produces
either worse code or a rule nobody follows.

## Consequences

### Positive

- The language the code is written in has a written definition, so behaviour
  questions have answers.
- A compiler or standard-library upgrade is far less likely to change behaviour
  silently.
- Non-standard constructs are caught by the build rather than by review.

### Negative

- Refusing convenient extensions occasionally costs code that is longer or
  slower than the compiler-specific version would have been.
- Where an extension is genuinely required, the encapsulation demanded above is
  itself work: an interface, a compiled-away fallback, and a seam that would not
  otherwise exist.
- Standard C++ does not deliver portability on its own, so this rule can create
  a false sense of security about code that is still full of
  implementation-defined assumptions.

### Neutral

- Which standard revision is targeted is a separate decision from writing
  standard C++ at all.
- Deliberate, documented subsetting for a constrained environment is compatible
  with this record; it is an extension of these guidelines, not a violation.

## Enforcement

Use an up-to-date C++ compiler (currently C++20 or C++17) with a set of options
that do not accept extensions.

## Related

- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — the general form of "localise it behind an interface".
- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — the compiler and
  analyser settings that make this checkable.
