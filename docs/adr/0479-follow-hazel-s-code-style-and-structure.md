# 0479. Follow Hazel's code style and structure

Status: accepted
Date: 2026-08-12

## Context

This repository grew its own conventions: lowercase nested namespaces
(`core::text`, `core::sln`, `core::fs`), `m_camelCase` members, accessors named
after the thing they return (`Name()`, `Spec()`, `Title()`), `[[nodiscard]]` and
`noexcept` on almost every const member, and `///` documentation comments that
in places ran longer than the code they described — 2575 such lines across 152
files.

TheCherno's Hazel is the reference this project is modelled on, and it reads
nothing like that: one namespace per project, `m_PascalCase`, `GetName()`,
tabs, no attribute decoration and no documentation comments. Working across the
two felt like working in two languages.

Two decisions here are not Hazel's, and are marked as such below, because
Hazel's practice on those points is an oversight rather than a choice.

## Decision

Match Hazel:

- One namespace per project, never nested: `Core` and `Graphite`.
- Data members `m_PascalCase`; statics `s_PascalCase`.
- Accessors are `GetX()`, or `IsX()`/`HasX()` when they return `bool`.
- Tabs, Allman braces except `namespace X {`, no column limit, enforced by
  `.clang-format`.
- No `[[nodiscard]]`, no `///` documentation comments.
- `Ref<T>`/`Scope<T>` and `CreateRef`/`CreateScope` in `Core/Base/Base.h`;
  `Timestep` rather than a bare `float`; an umbrella `Core.h`; `main` owned by
  `Core/App/EntryPoint.h` with the application supplying `CreateApplication`.
- The engine holds nothing the application owns. Solution-file parsing moved to
  `Graphite/src/Sln`, and the application grew its own doctest project.

Do not match Hazel on two points:

- **Keep `noexcept` on move constructors, move assignment and `swap`.** It is
  not decoration there: it decides whether `std::vector` moves or copies on
  reallocation.
- **Keep unnamed namespaces for file-local helpers**, per
  [0386](0386-sf-22-use-an-unnamed-anonymous-namespace-for-all-internal-non-exported-entities.md).
  Hazel uses a named `namespace Utils`, which has external linkage.

Error handling is untouched: `Result<T>` stays, per
[0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md).
Hazel throws and asserts; we do not.

## Rationale

A codebase modelled on another one pays a tax at every reading if it does not
look like it. The conventions being given up were defensible individually but
were not worth the cost of divergence — none of them changes what the program
does.

The two exceptions are where Hazel's practice has a mechanical cost rather than
an aesthetic one. Converting the 49 unnamed namespaces to a named `Utils` would
have made three helper pairs — `IsContinuation` in `Caret.cpp` and
`Encoding.cpp`, `LowercaseExtension` in `SolutionEditor.cpp` and
`SolutionReader.cpp`, `SortContent` in `CsprojReader.cpp` and
`VcxprojReader.cpp` — into ODR violations that the linker resolves silently and
arbitrarily.

## Consequences

### Positive

- One idiom across both projects and the reference implementation.
- `Core` no longer carries solution-file parsing, so the engine boundary
  [0002](0002-keep-core-free-of-ide-knowledge.md) draws is real rather than
  aspirational.
- The application has a test project for the first time; the 92 cases that
  moved out of `CoreTests` kept running instead of being orphaned.

### Negative

- **2575 lines of documentation were deleted**, and some of it recorded
  invariants and traps that the code does not state. This is the real cost of
  the change and it is not recoverable except from git history — commit
  `903bf94` isolates the deletion so it can be read back or partially reverted.
- Several hundred Core Guidelines records transcribed into this directory now
  describe a style the code no longer follows. `[[nodiscard]]` in particular is
  gone despite being sound advice.
- [0060](0060-c-37-make-destructors-noexcept.md) is contradicted in letter:
  explicit `noexcept` on destructors was stripped. No behaviour changed —
  destructors are implicitly `noexcept` — but the record no longer matches.
- `Core::Scope<T>` took a name the chord-scope enum held; that enum is now
  `Chord::Scope`.

### Neutral

- `Core` still links pugixml although nothing in it uses the library after the
  `Sln` move. Dead weight, left for a separate change.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — the boundary the `Sln` move
  finally makes true.
- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  explicitly not overridden.
- [0386](0386-sf-22-use-an-unnamed-anonymous-namespace-for-all-internal-non-exported-entities.md) —
  explicitly not overridden.
- [0060](0060-c-37-make-destructors-noexcept.md) — contradicted in letter.
