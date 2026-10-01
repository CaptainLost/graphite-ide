# Graphite

A code IDE in C++20 on Dear ImGui. Two projects: **Core**, the reusable engine,
and **Graphite**, the application composed on top of it.

## Before you change anything

Read [docs/adr/README.md](docs/adr/README.md). It indexes the decisions that
govern this repository — architecture and coding conventions alike, including
the C++ Core Guidelines as they apply here. Start with
[0001](docs/adr/0001-record-architecture-decisions.md), which explains when a
new record is written.

If you make a decision that outlives the change you are making, record it:

```powershell
.\scripts\New-Adr.ps1 "Title in the imperative"   # writes the record, adds the index row
.\scripts\Update-AdrIndex.ps1                     # rebuilds the index from the records
.\scripts\Test-Adr.ps1                            # checks every record and its index row
```

A record starts as `proposed`. Promoting it means editing the record's `Status:`
line and then running `Update-AdrIndex.ps1` — the index is derived from the
records, never hand-edited.

## Layout

| Path | What it holds |
|---|---|
| `Core/src/Core` | The engine: `App`, `Base`, `Command`, `Events`, `Fs`, `Git`, `ImGui`, `Platform`, `Text`, `UI` |
| `Core/tests` | doctest suite for the engine, built as `CoreTests` |
| `Graphite/src` | The application: `App`, `Editor`, `Panels`, `Settings`, `Sln`, `Workspace`. Everything but `Main.cpp` builds as the static library `GraphiteLib`; the `Graphite` executable is `Main.cpp` linked against it |
| `Graphite/tests` | doctest suite for the application, built as `GraphiteTests`, which links `GraphiteLib` |
| `vendor` | Dependencies as git submodules, built from source by premake |
| `scripts` | ADR tooling |
| `docs/adr` | Decision records |

## Build

Adding or removing a `.h`/`.cpp` requires regenerating the project files first,
because premake globs sources (`files { "src/**.h", "src/**.cpp" }`):

```powershell
& .\vendor\bin\premake\premake5.exe vs2026
```

The solution is `Graphite.slnx`. Generated project files are gitignored, so a
premake run never shows up in `git status`. A new file under `Graphite/src` is
in `GraphiteTests`' reach as soon as premake has run; there is no list of
application sources to add it to. MSBuild lives at
`C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`.

Build the test projects directly rather than through the solution, then run
them. There are two, and a change to the solution readers only shows up in the
second:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' Core\tests\CoreTests.vcxproj -p:Configuration=Debug -p:Platform=x64 -v:minimal -nologo -m
.\bin\Debug-windows-x86_64\CoreTests\CoreTests.exe

& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' Graphite\tests\GraphiteTests.vcxproj -p:Configuration=Debug -p:Platform=x64 -v:minimal -nologo -m
.\bin\Debug-windows-x86_64\GraphiteTests\GraphiteTests.exe
```

Core and CoreTests build with `warnings "High"` and `fatalwarnings { "All" }`,
so a clean build already proves there are no new warnings. Building Release too
is worthwhile — some warnings only fire with optimisation on.

Build with `-nodeReuse:false`. Without it MSBuild leaves worker processes behind
that hold open handles to the sources, and the next `git checkout` or branch
switch fails partway with `unable to unlink old ...: Invalid argument`.

Paths in `postbuildcommands` must be relative to the project, not written
through `%{wks.location}`: premake emits that token as MSBuild's
`$(SolutionDir)`, which points at the repository root only when the build goes
through the solution. Building a `.vcxproj` directly — which is what the
commands above do — sets it to the project's own directory instead.

**Both doctest filters fail open.** `-sf=*FooTests.cpp` matches nothing in this
build, and `-tc` splits its argument on commas, so a test name containing one
selects nothing. Either way the run reports success having executed zero cases.
When a filtered run passes, check that it actually ran something; when in
doubt, run the whole suite.

## ADR tooling

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/NewAdrTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/TestAdrTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/UpdateAdrIndexTests.ps1
```

Plain PowerShell 5.1, no external modules. `Test-Adr.ps1` exits non-zero when a
record is malformed or missing from the index, so it is safe to wire into a
hook or a check.

`scripts/Test-NoPathString.ps1` fails on any `.string()` in `Core/src` or `Graphite/src` not marked `// not a path`; paths go through `Core::ToUtf8`.
