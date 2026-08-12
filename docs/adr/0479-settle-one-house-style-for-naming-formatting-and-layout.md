# 0479. Settle one house style for naming, formatting and layout

Status: accepted
Date: 2026-08-12

## Context

The two projects had drifted into dialects of the same language. Namespaces were
lowercase and nested three deep in places (`core::text`, `core::sln::edit`) while
the application used a flat `graphite`. Data members were `m_camelCase` and
public struct fields were plain `camelCase`, so a field changed spelling
depending on which side of the `public:` it sat on. Accessors were named three
ways at once: `GetWindow()` beside `Spec()` beside `Id()`. Roughly 2500 lines of
`///` documentation had accumulated, in places longer than the code it
described, and `[[nodiscard]]` and `noexcept` decorated nearly every const
member.

None of it was wrong. All of it was inconsistent, and the inconsistency was the
cost: every read of an unfamiliar file started by working out which dialect it
was written in.

`Core` also still held `Sln`, ten files that parse Visual Studio solution and
project formats — knowledge the application owns, sitting in the library that
[0002](0002-keep-core-free-of-ide-knowledge.md) says must not know it is an IDE.

## Decision

One style, applied everywhere:

- **One namespace per project**, never nested: `Core` and `Graphite`. A name that
  would collide is renamed or nested inside the type that owns it — the chord
  scope enum became `Chord::Scope` when `Scope<T>` took the plain name.
- **PascalCase for every data member**, private (`m_PascalCase`), static
  (`s_PascalCase`) and public (`PascalCase`) alike.
- **Accessors are `GetX()`**, or `IsX()`/`HasX()` when they return `bool`. A
  function that computes, allocates or performs I/O is not an accessor and does
  not take those prefixes: `Materialize()`, `EncodeToBytes()`, `TakeSnapshot()`,
  `CheckChangedOnDisk()`, `ComputeSolutionStamp()`.
- **Tabs, Allman braces except after `namespace`, no column limit**, enforced by
  `.clang-format`. The configuration is idempotent: a second run changes nothing.
- **No `[[nodiscard]]` and no `///` documentation comments.** A comment that
  records a non-obvious invariant, a platform quirk or a trap is written as a
  plain `//` comment and is kept.
- **Shared vocabulary in `Core/Base/Base.h`**: `Scope<T>`/`Ref<T>` with
  `CreateScope`/`CreateRef`, used in preference to the raw smart pointers; a
  `Timestep` rather than a bare `float`; an umbrella `Core.h`; `main` owned by
  `Core/App/EntryPoint.h` with the application supplying `CreateApplication` and
  `CreateLogSpec`.
- **The engine holds nothing the application owns.** `Sln` moved to
  `Graphite/src/Sln`, and the application grew its own doctest project rather
  than letting those tests be orphaned.

Two exceptions, because in both cases the general rule would cost something
mechanical rather than aesthetic:

- **`noexcept` stays on move constructors, move assignment and `swap`.** There it
  is not decoration: it decides whether `std::vector` moves or copies its
  elements when it reallocates.
- **File-local helpers stay in unnamed namespaces**, per
  [0386](0386-sf-22-use-an-unnamed-anonymous-namespace-for-all-internal-non-exported-entities.md).

Error handling is untouched: `Result<T>` stays, per
[0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md).

## Rationale

Consistency is the whole benefit, and it only pays once it is total — a rule
followed in four files out of five still forces the reader to check. None of
these changes alters what the program does, which is what made it affordable to
apply them all at once.

The two exceptions are where the general rule has a measurable cost. Giving the
49 unnamed namespaces external linkage would have turned three helper pairs —
`IsContinuation` in `Caret.cpp` and `Encoding.cpp`, `LowercaseExtension` in
`SolutionEditor.cpp` and `SolutionReader.cpp`, `SortContent` in
`CsprojReader.cpp` and `VcxprojReader.cpp` — into ODR violations that the linker
resolves silently and arbitrarily.

The accessor rule earned its exception the hard way. Applying it mechanically
produced `GetText()` for a call that materialises an entire piece table,
`GetSnapshot()` for one that takes a lock and copies a 4096-entry buffer every
frame, and `HasChangedOnDisk()` for two filesystem stats made inside a loop. A
name that promises a field read and delivers I/O is worse than the inconsistency
it replaced, so cost is now part of the naming rule rather than an afterthought.

## Consequences

### Positive

- One idiom across both projects; no dialect to identify before reading.
- `Core` no longer carries solution-file parsing, so the boundary
  [0002](0002-keep-core-free-of-ide-knowledge.md) draws is real rather than
  aspirational.
- The application has a test project for the first time; the 94 cases that moved
  out of `CoreTests` kept running instead of being orphaned.
- A minimized window no longer runs a full update-and-render frame.

### Negative

- **About 2500 lines of documentation were deleted**, and some recorded
  invariants the code does not state. Eight of the worst losses were restored as
  plain comments; the rest is recoverable only from history, where commit
  `7b8b913` isolates the deletion.
- Several hundred Core Guidelines records transcribed into this directory now
  describe a style the code no longer follows. `[[nodiscard]]` in particular is
  gone despite being sound advice.
- [0060](0060-c-37-make-destructors-noexcept.md) is contradicted in letter:
  explicit `noexcept` on destructors was stripped. No behaviour changed —
  destructors are implicitly `noexcept` — but the record no longer matches.

### Neutral

- The style was informed by reading a mature engine codebase of similar shape.
  What survived into this record are the rules that could be justified here, on
  their own terms; the rest was dropped.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — the boundary the `Sln` move
  finally makes true.
- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  explicitly not overridden.
- [0386](0386-sf-22-use-an-unnamed-anonymous-namespace-for-all-internal-non-exported-entities.md) —
  explicitly not overridden.
- [0060](0060-c-37-make-destructors-noexcept.md) — contradicted in letter.
