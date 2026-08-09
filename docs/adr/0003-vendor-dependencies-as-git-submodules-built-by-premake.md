# 0003. Vendor dependencies as git submodules built by premake

Status: accepted
Date: 2026-08-09

## Context

Six dependencies are in use: Dear ImGui (branch `docking`), GLFW, doctest,
libgit2, pugixml and nlohmann/json. Each is a git submodule under `vendor/`,
declared in `.gitmodules`. The four that need compiling have a build script in
`vendor/premake/` — `glfw.lua`, `imgui.lua`, `libgit2.lua`, `pugixml.lua` —
included from the root `premake5.lua` under a `Dependencies` group; doctest and
json are header-only and enter through `IncludeDir` alone.

`premake5.exe` is itself committed, under `vendor/bin/premake/`, with an
explicit un-ignore rule in `.gitignore`. `GenerateProjects.bat` runs it. The
Material Design Icons font under `vendor/fonts/` is committed directly rather
than being a submodule, since it is two files and has no upstream build.

Generated project files — `*.vcxproj`, `*.slnx`, `*.sln` — are gitignored, so
the solution is a build artefact, not a checked-in file.

## Decision

A dependency enters this repository as a git submodule under `vendor/` and is
built from source as a project in the generated solution. No package manager,
no prebuilt binaries, no system-installed libraries.

The one committed binary is `premake5.exe`, because it is what generates the
build in the first place and cannot bootstrap itself.

A dependency that is header-only gets an `IncludeDir` entry and no premake
script. A dependency that needs compiling gets a script in `vendor/premake/`
that describes it as a static library, built with the same architecture and
runtime settings as everything else, but **without** warnings-as-errors — that
setting applies to Core and Graphite only.

## Rationale

Building dependencies from source in the same solution means their sources are
steppable in the debugger alongside our own code, which is worth a great deal
when the bug is on the boundary between our call and libgit2's behaviour. It
also means the build has no network step and no external tool: clone with
`--recursive`, run `GenerateProjects.bat`, build.

## Consequences

### Positive

- A single build system for everything; no manifest, lockfile or package cache
  to keep in step with the solution.
- Dependency sources are debuggable and greppable in the same tree, which is
  how several of the ImGui and libgit2 questions in this project got settled.
- The exact upstream commit is pinned by the submodule, so a build is
  reproducible from the repository alone.

### Negative

- A clone without `--recursive` produces a tree that fails to generate, with an
  error that does not name the real cause. This is the most common way to lose
  half an hour on this repository.
- Full builds are slower, because GLFW, ImGui, libgit2 and pugixml compile from
  source rather than arriving prebuilt.
- Updating a dependency is a submodule bump plus, sometimes, a change to its
  premake script when its file layout moves. A package manager would make that
  a version-number edit.
- Every new dependency that is not header-only costs a hand-written premake
  script before it can be used at all, which is a real deterrent to adding one.
  That deterrent is not entirely unwelcome.

### Neutral

- `premake5.exe` in the repository is a committed binary, which is normally a
  smell. It is accepted here because it is the bootstrap.
- The generated solution being gitignored means a premake run never shows up in
  `git status`, so adding a source file requires remembering to regenerate.

## Alternatives Considered

### vcpkg or Conan

Far less to maintain: no premake script per dependency, and updates are a
version edit. Rejected on two counts. It adds a toolchain the project does not
otherwise need, on top of premake, which is already the thing generating the
build. And it delivers dependencies as binaries, which puts their sources out
of the debugger's reach — the specific capability this project has used most.

### Committing dependency sources directly instead of as submodules

Removes the `--recursive` trap entirely, and a fresh clone always builds. Loses
the pin to an upstream commit, makes updating a manual copy, and puts several
hundred thousand lines of other people's code into this repository's history.
Rejected: the trap is a one-time cost per clone, the history is forever.

### System-installed libraries found by the build

Cheapest build, no vendored code at all. Rejected outright: it makes the build
depend on the state of the machine, which is the failure mode all of the above
are trying to avoid.

## Related

- [0001](0001-record-architecture-decisions.md)
- [0005](0005-target-c-20-with-headers-not-modules.md) — the language settings
  these scripts apply.
