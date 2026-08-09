# 0002. Keep Core free of IDE knowledge

Status: accepted
Date: 2026-08-09

## Context

The repository is two projects. **Core** is a static library holding `App`,
`Base`, `Event`, `Fs`, `Git`, `Gui`, `Platform`, `Sln`, `Text` and `Ui`.
**Graphite** is the application: `Workspace`, the panels, the editor windows,
settings. The split only earns its keep if the dependency runs one way, and the
thing that depends on it is `Core/tests` — a doctest suite that builds and runs
without a window, which is only possible because the logic it covers does not
reach for one.

Two things were true when this was written, and they are different in kind.
Structurally the boundary holds: no file under `Core/src` includes a header from
`Graphite/src`. Textually it leaks: `Application.h` defaults its application
name to `"IDE"`, `Window.h` defaults its title to `"IDE"`, `Theme.h` describes
itself as the "Dark IDE theme", and comments in `JsonFile`, `RingBufferSink`,
`DirectoryWatcher` and `DocumentSet` explain themselves in terms of the IDE
above.

In the other direction, Graphite includes `<imgui.h>` and `<imgui_internal.h>`
directly in more than a dozen files — panels, `TextEditorWidget`,
`DocumentWindows`, `StatusBar` — while touching no GLFW or OpenGL header.

## Decision

Core does not depend on Graphite. No header, no type, no function, no build
setting in Core refers to anything Graphite defines, and the dependency is
never inverted "just for this one case".

Graphite may use Dear ImGui directly; it may not touch the windowing or
rendering backend. GLFW and OpenGL are reached only through
`Core/Platform/Window.h` and `Core/Gui/ImGuiRenderer.h`.

The test of the rule is a substitution: replacing the backend must mean
replacing `GlfwWindow` and `ImGuiRenderer`, and nothing in Graphite.

Naming is held to a weaker standard than dependency. A default string or a
comment that says "IDE" does not break the build of anything and does not make
Core untestable; it is untidy, not structural. Such occurrences are fixed when
the surrounding code is touched, and are not worth a sweep of their own.

## Rationale

The boundary is not an aesthetic preference — it is what makes `Core/tests`
possible. Every test in that suite runs headless, and it can only do so because
`FileTree`, `TextBuffer`, `SlnxEditor` and the rest were written without a
panel to render into. An upward dependency, admitted once, takes that away
quietly: the first Core type that needs a `Workspace` makes the whole library
need one.

ImGui is deliberately on Graphite's side of the line, unlike GLFW and OpenGL.
ImGui *is* the UI vocabulary this application is written in; forcing panels to
address it through a Core-owned wrapper would mean maintaining a facade over a
large, fast-moving immediate-mode API for no benefit. The backend is different:
it is small, it is genuinely swappable, and it is the thing a port would
replace.

## Consequences

### Positive

- `Core/tests` runs without a window, which is why the parsers, the file tree
  and the text buffer have tests at all — the Graphite project has none.
- A rendering-backend change is bounded to two files in Core.
- Core stays reusable by something that is not an IDE, which is the only reason
  the split exists.

### Negative

- Panels and Core communicate through `EventBus` where a direct call would do,
  which costs a hop and an indirection at every such point.
- Some Core APIs are more general than their single caller needs, because they
  are not allowed to know who the caller is.
- The rule is not machine-enforced. Nothing fails the build when a Core header
  starts including a Graphite one; it holds by review only.

### Neutral

- The "IDE" strings and comments in Core stay for now, recorded above so the
  next reader knows they are known and tolerated rather than overlooked.
- Graphite's direct use of `imgui_internal.h` is permitted by this record, and
  it is a real hazard for a different reason: it is an unstable API across ImGui
  updates. That is a dependency question, not a boundary question.

## Alternatives Considered

### One project, no split

Simpler to build and to navigate, and nothing would need `EventBus`. It gives
up the headless test suite: the logic would sit in the same binary as the
windowing code and could not be exercised without starting a window. That is
the whole value being traded, so it loses.

### Split, with an upward dependency permitted for special cases

Keeps the structure while allowing Core to call back into the application when
it is convenient. This is the shape such boundaries dissolve into: the exception
is never revoked, and after the second one the direction is no longer a rule but
a habit. Rejected because a boundary that bends on request is not a boundary.

### A Core-owned wrapper over ImGui as well

Would put every UI call behind Core's interface and make Graphite backend-blind
too. Rejected: ImGui's surface is enormous and changes often, so the wrapper
would be a permanent maintenance cost, and the substitution it would enable —
replacing ImGui itself — is not a change this project plans to make.

## Related

- [0001](0001-record-architecture-decisions.md)
- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  how errors cross the boundary this record draws.
- `docs/superpowers/specs/2026-08-06-cpp-imgui-ide-design.md`, which first
  stated the rule as "Core contains not a single string specific to the IDE".
  That version was stronger than what the code does; this record says what is
  actually held to.
