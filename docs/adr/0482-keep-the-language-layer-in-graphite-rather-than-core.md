# 0482. Keep the language layer in Graphite rather than Core

Status: proposed
Date: 2026-08-13

## Context

Syntax colouring needed somewhere to put a language registry, a C++ lexer, and
the cache that keeps a document's lexed state — none of which existed before
this stage. [0002](0002-keep-core-free-of-ide-knowledge.md) draws the line
between the two projects: Core is the headless, reusable engine, tested
without a window; Graphite is the application, and it alone knows what an IDE
is. That record already anticipated a tension it did not resolve — its
`Related` section points forward to whatever eventually needs project
knowledge, without saying which side gets it.

Syntax colouring on its own needs nothing Core does not already have:
`Core::TextBuffer`, `Core::ColumnCount` and a string view are enough to lex a
line and draw its tokens. But this stage is not colouring in isolation — it is
the first slice of a language layer that the plan already names as
`Graphite/src/Lang`, and the next slices past colouring are the ones that
settle where it lives. A semantic analyser — the thing that will eventually
say what a symbol under the caret resolves to — needs include paths,
preprocessor definitions and a file list. Those come from parsing `.vcxproj`
and `.csproj`, and the readers that already do that — `VcxprojReader`,
`CsprojReader`, `SolutionReader` — sit in `Graphite/src/Sln`, precisely
because that parsing is IDE knowledge Core is not allowed to have.

ADR 0002 records one objection to drawing Graphite's boundary this way: unlike
Core, Graphite had no tests, so logic placed there was logic placed somewhere
unverifiable. That was true when 0002 was written. It no longer is —
`Graphite/tests` builds as `GraphiteTests`, runs headless like `CoreTests`,
and this stage alone added `CppLexerTests.cpp`, `SyntaxCacheTests.cpp` and
`LanguageRegistryTests.cpp`, on the order of forty cases between them, none
touching a window.

## Decision

The language layer lives in `Graphite/src/Lang`, beside `Graphite/src/Sln`:
`Lang/Api` (`LanguageId`, `TokenKind`), `Lang/Registry` (`LanguageRegistry`,
mapping a path to a `LanguageId` and a `LanguageId` to a `Lexer`), and
`Lang/Syntax` (`Token`, `LineState`, `CppLexer`, `SyntaxCache`). It depends on
`Core::Text` for `TextBuffer` and `Core::ColumnCount`, the same direction
every other part of Graphite already depends on Core.

One piece is deliberately outside `Lang`: `SyntaxPalette`
(`Graphite/src/Editor/SyntaxPalette.h`) maps a `TokenKind` to an `ImU32`, which
means it includes `<imgui.h>`. `Lang` stays drawable-agnostic — nothing under
it names a colour or a draw call — specifically so that `GraphiteTests`, which
links `Lang` and not the editor widgets, can exercise the lexer and the cache
without linking ImGui.

## Rationale

The readers that will feed an analyser project knowledge already live in
Graphite, in `Sln`, because ADR 0002 put them there. Putting `Lang` beside
them keeps one project holding all of "what does this codebase's build
actually contain" rather than splitting that knowledge across a boundary for
a stage that does not yet need to. The alternative — inventing a
Core-owned, build-system-neutral description of a project purely so `Lang`
could sit in Core and consume it — launders a dependency across the boundary
without removing it: something still has to turn a `.vcxproj` into that
neutral shape, and that something is exactly the IDE knowledge 0002 keeps out
of Core.

The tests objection is answered directly, not argued around: `GraphiteTests`
existing and this stage's roughly forty headless cases show that "no tests"
is no longer a property of code placed in Graphite.

## Consequences

### Positive

- `Lang` sits next to the `Sln` readers a later analyser will need, so
  reaching `.vcxproj` data is a normal Graphite-to-Graphite call, not a
  boundary crossing.
- Core gains no C++-language knowledge and no ImGui dependency; `CoreTests`
  is unaffected by this stage, which this task's build proves by running it
  unchanged.
- `Lang` is independently testable without a window because `SyntaxPalette`
  was kept out of it, so `CppLexerTests` and `SyntaxCacheTests` run in
  `GraphiteTests` the same way `Core`'s tests run in `CoreTests`.

### Negative

- Graphite grows a second sizeable module family (`Lang`, alongside `Sln`)
  that Core cannot reuse should a future headless consumer of this
  repository's code want colouring or project parsing without the rest of
  the IDE. Nothing today asks for that, but the option is closed by this
  decision, not left open.
- The boundary this record relies on — Graphite may depend on Core, never
  the reverse — is enforced the same way ADR 0002 already admits its own is:
  by review, not by the build. `Lang` including a Core header the wrong
  direction would not fail to compile on that basis alone.
- `GraphiteTests` having tests now does not mean it has ADR 0002's other
  property, a build that runs without a window at the application level —
  `Graphite.exe` itself still needs GLFW and a monitor. Only the logic
  factored into `Lang` and `Sln` is headless; this record does not claim
  more than that.

### Neutral

- This record narrows ADR 0002's `Related` pointer rather than reopening it:
  0002 still governs the Core/Graphite split in general, and this record
  answers, for the language layer specifically, which side of that split it
  falls on.
- Nothing here builds or promises the semantic model an analyser would need.
  `LanguageRegistry` maps a path to a lexer for colouring; it does not
  resolve a symbol, and this record does not claim otherwise.

## Alternatives Considered

### Move the project readers into Core, and `Lang` alongside them

Would let `Lang` and `Sln` share a home without crossing 0002's boundary in
either direction. Rejected outright: `.vcxproj` and `.csproj` parsing is
exactly the IDE knowledge 0002 forbids Core from holding, and moving it there
to make room for `Lang` would undo the record that this decision is meant to
respect, not just sidestep it.

### Have Core define a neutral project description that Graphite fills in

Core would declare a shape — include paths, defines, a file list — with no
knowledge of `.vcxproj`, and Graphite's `Sln` readers would populate it before
handing it to a Core-side analyser. This keeps the analyser in Core, reusable
by a hypothetical second application. Rejected for now as ceremony with one
consumer: nothing outside Graphite exists to reuse an analyser, so the neutral
type would be maintained without ever discharging the reason it was
introduced. Worth revisiting if Core ever gains a second consumer that needs
project knowledge in this shape — at which point this record would be
superseded, not edited.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — the boundary this record
  places the language layer on the Graphite side of, and whose tests
  objection this record closes.
- [0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md) — the
  shape of the lexer this layer holds.
- `Graphite/src/Sln` — the readers `Lang` sits beside and will eventually
  draw project data from.
