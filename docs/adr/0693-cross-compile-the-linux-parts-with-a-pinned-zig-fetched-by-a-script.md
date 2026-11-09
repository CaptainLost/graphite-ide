# 0693. Cross-compile the Linux parts with a pinned zig fetched by a script

Status: proposed
Date: 2026-11-09

## Context

WSL support runs a helper inside the distro, built from Core and libgit2, so the repository
now produces Linux binaries as well as Windows ones. The user chose to build them from
Windows, in the same build as everything else, so that no machine needs a provisioned distro
to build Graphite; WSL is needed only to run the Linux tests. The helper must not depend on
the distro's glibc, which differs between distros and versions.

zig bundles clang, libc++ and musl for every target and cross-compiles from Windows without
a sysroot. A spike on 2026-11-09 built C++20 with `<format>` and all of libgit2 for
`x86_64-linux-musl` with zig 0.17.0, and the static binaries ran in Ubuntu on WSL 2. The
release is 100 MB zipped and 361 MB unpacked.

## Decision

Build every Linux binary with zig, version 0.17.0, pinned in `scripts/Get-Zig.ps1` by its URL
and its published SHA-256. The script downloads the release, refuses it unless the checksum
matches, and unpacks it into `vendor/bin/zig`, which git ignores; it does nothing when the
pinned version is already there (a `.pinned-version` marker beside `zig.exe`). Link the Linux
binaries statically against musl. Upgrading zig means changing the version and the checksum
together, and nothing else.

## Rationale

One pinned toolchain fetched on demand keeps the build Windows-only and reproducible without
putting hundreds of megabytes into the history. musl links statically without the home
directory lookup problems static glibc has, which libgit2 needs for `~/.gitconfig`.

## Consequences

### Positive

- A fresh clone builds the Linux parts after one script run, with no distro and no package
  manager.
- Every machine compiles with the same compiler version.

### Negative

- The first build on a machine needs the network to fetch zig, and 361 MB of disk.
- zig's build API changes between releases, so an upgrade can mean rewriting `build.zig`.

### Neutral

- The Windows build keeps MSVC; zig builds only what runs on Linux.

## Alternatives Considered

- **Commit zig under `vendor/bin`.** Lost: 361 MB per version in the history for ever.
- **Build inside a distro with its gcc or clang.** Lost: every machine that builds Graphite
  would need a provisioned distro, which the user rejected.
- **clang with a vendored Linux sysroot.** Lost: assembling and pinning a musl sysroot and
  libc++ by hand is what zig already ships, tested.

## Related

- [0688](0688-refuse-to-watch-a-folder-inside-wsl-through-windows.md)
