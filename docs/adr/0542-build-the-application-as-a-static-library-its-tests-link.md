# 0542. Build the application as a static library its tests link

Status: proposed
Date: 2026-10-01

## Context

`Graphite` was a single executable project, so there was no library for
`GraphiteTests` to link the code under test out of. The test project compiled
the application's sources into itself instead: whole folders where they were
headless (`Sln`, `Lang`, `Settings`, `Git`, ...), and `Editor`, `Panels`,
`Workspace` and `App` file by file, with a `removefiles` for the one `Git` unit
whose link dragged the editor in. Every new application file a test needed
had to be added to that list by hand, and forgetting it surfaced as an
unresolved external rather than a message naming the cause. The list carried
some thirty lines of comments justifying its entries.

## Decision

Build everything under `Graphite/src` except `Main.cpp` as the static library
`GraphiteLib`. The `Graphite` executable compiles `Main.cpp` alone and links
`GraphiteLib`; `GraphiteTests` compiles only `Graphite/tests/src` and links
`GraphiteLib` too. Both application projects take their compile options from one
function in `Graphite/premake5.lua`, so the library is compiled exactly as the
executable used to be. Nothing under `Graphite/src` may rely on a static
initialiser registering itself.

## Rationale

The linker takes from a static library only the objects something references,
which is the selection the hand-kept list made, done by the tool that knows the
answer. A test that calls a headless function pulls in that unit and what it
reaches; a window nobody calls stays out without a `removefiles`.

## Consequences

### Positive

- A new file under `Graphite/src` is in the tests' reach once premake has run;
  there is no second list to update.
- The application's code is compiled once per configuration, not once for the
  executable and again for the tests.

### Negative

- An object that nothing names is dropped from the executable as well as from
  the tests, so a self-registering static initialiser would silently vanish.
  None exists today; adding one needs `/WHOLEARCHIVE:GraphiteLib` on both
  consumers.
- A test that reaches a function in a widget unit now links that unit and
  whatever it pulls in, where before the build refused. The link still
  succeeds — every library the executable needs is on `GraphiteTests`' link
  line — so keeping tests headless is a matter of review, not of the build.

### Neutral

- The executable's generated project is unchanged apart from its source list
  and a reference to `GraphiteLib`: defines, include directories, warnings,
  runtime, postbuild copies and `debugdir` are the same.

## Alternatives Considered

- **Keep the hand-kept list.** It works, but costs a broken link and a comment
  for every file a test needs, and it duplicates a decision the linker makes.
- **Compile all of `Graphite/src` into `GraphiteTests` by glob.** It removes the
  list but compiles the application twice, and every object file is linked
  whether a test reaches it or not, so `Main.cpp` (whose `main` comes from
  Core's `EntryPoint.h` and collides with the test runner's) still needs a
  `removefiles` and every window lands in the test binary.

## Related

- ADR 0482 — `GraphiteTests` and the headless `Lang` layer.
- ADR 0003 — dependencies as premake-built static libraries.
