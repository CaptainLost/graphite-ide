# 0008. P.2: Write in ISO Standard C++

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The rule this repository argues with most directly. Graphite is MSVC-only and
reaches Win32 in eight files across `Fs`, `Platform` and `Gui`, so the question
is not whether extensions are used but whether they are contained.

## Decision

Adopt P.2 as written, relying on its own Note about localized extensions.
Transcribed in full:

> **Reason.** This is a set of guidelines for writing ISO Standard C++.

**Note.** There are environments where extensions are necessary, e.g., to access
system resources. In such cases, localize the use of necessary extensions and
control their use with non-core Coding Guidelines. If possible, build interfaces
that encapsulate the extensions so they can be turned off or compiled away on
systems that do not support those extensions.

Extensions often do not have rigorously defined semantics. Even extensions that
are common and implemented by multiple compilers might have slightly different
behaviors and edge case behavior as a direct result of not having a rigorous
standard definition. With sufficient use of any such extension, expected
portability will be impacted.

**Note.** Using valid ISO C++ does not guarantee portability (let alone
correctness). Avoid dependence on undefined behavior (e.g., undefined order of
evaluation) and be aware of constructs with implementation defined meaning
(e.g., `sizeof(int)`).

**Note.** There are environments where restrictions on use of standard C++
language or library features are necessary, e.g., to avoid dynamic memory
allocation as required by aircraft control software standards. In such cases,
control their (dis)use with an extension of these Coding Guidelines customized
to the specific environment.

**Enforcement.** Use an up-to-date C++ compiler (currently C++20 or C++17) with
a set of options that do not accept extensions.

## Rationale

Adopted in the form the rule itself permits: platform code is allowed, and it is
localized behind an interface. `Window`, `FileSystem`, `FileDialog`, `Shell` and
`Process` are exactly the "interfaces that encapsulate the extensions" the Note
asks for, and swapping the platform means replacing what is behind them.

What is not met is the enforcement half. A single compiler cannot tell you which
of your code is standard; it tells you which of your code it accepts. Meeting
P.2's enforcement would mean a second compiler in CI, which this project does
not have and is not planning.

## Consequences

### Positive

- The platform boundary already exists and has a written reason, so the next
  Win32 call has an obvious place to go.

### Negative

- "ISO Standard C++" is asserted, not demonstrated. Any non-standard construct
  MSVC happens to accept is invisible until someone tries another compiler, and
  by then it is spread.
- `/utf-8`, `NOMINMAX` and `WIN32_LEAN_AND_MEAN` are compile settings the code
  silently depends on; a build without them fails in ways that do not point at
  this rule.

### Neutral

- Portability is not a goal of this project. The rule is adopted for the
  discipline of containment, not because a port is planned.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| Win32 reached from a rendering file rather than a platform one | `Core/src/Core/Gui/ImGuiRenderer.cpp` | The least localized of the eight; the other seven are in `Fs` and `Platform`, where the rule expects them |
| No second compiler ever builds this code | whole repository | Per [0005](0005-target-c-20-with-headers-not-modules.md); P.2's enforcement is unmet by choice |

## Related

- [0007](0007-p-1-express-ideas-directly-in-code.md) — the previous rule.
- [0009](0009-p-3-express-intent.md) — the next rule.
- [0005](0005-target-c-20-with-headers-not-modules.md) — the toolchain this is
  measured against.
- [0002](0002-keep-core-free-of-ide-knowledge.md) — the boundary the platform
  code sits behind.
