# 0488. Ship clangd with the release

Status: superseded by 0577
Date: 2026-08-16

## Context

[0485](0485-take-code-intelligence-from-external-language-servers.md) commits
this project to launching an external language server rather than writing its
own front end. That server has to come from somewhere before `FindServerBinary`
can find it. Three places are possible: a path the user types into settings, a
copy installed on the machine already and reachable through `PATH`, or a copy
this repository ships. The first two both require the user to have already
solved "get clangd onto this machine" before the feature does anything —
installing LLVM, or a standalone clangd release, or building one — which is a
real obstacle for someone who opened this IDE to get C++ intelligence, not to
go find a compiler toolchain first.

`FindServerBinary` (added alongside this record, in
`Graphite/src/Lang/Server/ServerDiscovery.cpp`) already checks three
locations in order: a configured path, a binary next to the application's own
executable, and `PATH`. The second of those only ever finds anything if a
binary is placed there — this record decides that this repository is what
places it.

## Decision

`clangd.exe` ships beside `Graphite.exe`, copied into the target directory by
`Graphite/premake5.lua`'s `postbuildcommands`, from `vendor/bin/clangd/` —
the same tier this repository already uses for a tool it ships rather than
compiles, alongside `vendor/bin/premake`. `FindServerBinary` checks a
configured path first, then this bundled copy beside the executable, then
`PATH`. A user who has installed their own clangd and put it on `PATH`, or
who has pointed settings at a specific version, is not overridden by the
bundled one; the bundled copy exists to answer for everyone who has done
neither.

The copy step is conditional on the file existing:
`if exist "..\vendor\bin\clangd\clangd.exe" copy /Y ...`, not an unconditional
`{COPYFILE}`. The binary itself is not committed to the repository — it is
roughly a hundred megabytes, and this record does not change
[0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md)'s
build-from-source policy for what is compiled, only adds a place for a tool
that is downloaded pre-built. `vendor/bin/clangd/README.md` names where a
maintainer or release process puts one. A build with no binary there still
succeeds; `FindServerBinary` reports `NotFound` and the status bar shows
`clangd: not found`, which is the intended degraded state, not a build
failure.

## Rationale

The feature this repository is building — go to definition, find references,
diagnostics — has to work the first time someone opens a C++ file, not after
a side quest to install a compiler toolchain. clangd's licence (Apache 2.0
with LLVM exceptions) permits redistributing the built binary, so nothing
stops this repository from being the place that solves "get clangd onto this
machine" once, for every install, instead of asking every user to solve it
themselves. Checking the configured path and `PATH` ahead of the bundled copy
costs nothing extra to keep — `FindServerBinary`'s three-tier order was
already the shape a settings override needs, and a bundled default is just
what fills the last tier instead of leaving it empty.

## Consequences

### Positive

- C++ intelligence works on install, with no separate download or PATH setup,
  for the common case of "whatever clangd version this release shipped with".
- The settings path and `PATH` overrides are not new work this decision adds —
  they already had to exist, because every other language server this project
  will ever add installs differently and none of them can be bundled the same
  way `clangd` can for this one platform build.

### Negative

- The release grows by roughly a hundred megabytes for a binary this
  repository does not build and cannot patch — a `clangd` bug is a wait for
  upstream, not a fix here.
- The bundled version needs to be kept current by hand; there is no automated
  check that it has drifted from a current LLVM release, so it can go stale
  silently.
- The same repository can now behave differently for two developers: one
  whose `PATH` has a different clangd version answers questions the bundled
  copy would have answered differently for someone else, and nothing surfaces
  that disagreement — both look like "clangd is running" on the status bar.

### Neutral

- This decision is Windows-specific as written — the copy step and the
  binary's `.exe` extension both assume the platform this repository
  currently targets. A second platform needs its own binary and its own
  conditional copy, not a change to this one.

## Alternatives Considered

### Require the user to install clangd and configure a path

The zero-effort option for this repository: `FindServerBinary`'s configured
path and `PATH` checks already make it work if the binary exists anywhere.
Rejected as the *only* mechanism because it makes "install this IDE" and "get
C++ intelligence" two separate tasks, the second of which most users will not
know they need to do until they notice go-to-definition doing nothing — a
silent-until-investigated failure rather than a working feature.

### Download clangd on first run

Fetch the binary from the network the first time it is needed, cache it, and
avoid growing the release at all. Rejected: it trades a build-time cost for a
runtime one — the first real use of the feature now depends on network
access and picks a moment to stall that the user did not choose — and it adds
a downloader, a version manifest and an update path that a straight copy step
does not need. Worth reconsidering if the binary's size becomes a bigger
problem than the download latency would be.

## Related

- [0485](0485-take-code-intelligence-from-external-language-servers.md) — the
  decision to depend on an external server at all, which this record supplies
  a binary for.
- [0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md) — the
  build-from-source policy this record does not change; `clangd` is
  downloaded pre-built, not compiled from a submodule.
