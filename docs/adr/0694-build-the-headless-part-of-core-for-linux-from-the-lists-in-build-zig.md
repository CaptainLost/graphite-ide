# 0694. Build the headless part of Core for Linux from the lists in build.zig

Status: proposed
Date: 2026-11-09

## Context

The WSL helper runs inside the distro and is built from Core and libgit2 (0693). Core as a
whole cannot build for Linux: it links ImGui, GLFW and OpenGL, and much of `Platform`,
`FileSystem` and `Terminal` is Win32. What the helper needs first — reading and writing
repositories — is `Git` without `Git/Command`, the parts of `FileSystem` Git uses, and `Base`.
On 2026-11-09 those built for `x86_64-linux-musl` with few changes: `FileSystem.cpp` already
had non-Windows branches, and the Git code had no Win32 at all outside `Git/Command`.

Sources are laid out by feature (0550), so a "headless" folder would split features apart.

## Decision

No file moves. `build.zig` at the repository root names the Linux source set in two pairs of
lists — sources and tests — each a set of folders walked for `.cpp` files plus single files,
minus exclusions (a trailing `/` excludes a folder). It declares every walked folder to zig,
so a file added to one is picked up without editing `build.zig`. Today the set is `Base`,
`FileSystem/{AtomicWrite,FileName,FileSystem,RelativePath,WslSharePath}.cpp` and `Git` minus
`Git/Command/`, with their tests.

Platform code stays where it is, split by `CORE_PLATFORM_WINDOWS` and `CORE_PLATFORM_LINUX`;
`PlatformDetection.h` defines the latter for x86-64 Linux. A test of Windows behaviour is
wrapped in `#ifdef CORE_PLATFORM_WINDOWS` with a line saying why, and gets a Linux
counterpart where Linux has its own behaviour to pin. Linux sources compile with
`-Wall -Wextra -Werror`, less `-Wmissing-field-initializers` (designated initializers that
leave defaulted fields out are this codebase's idiom). On Linux, path keys keep case, and a
rewritten file keeps its permission bits.

## Rationale

Walking folders mirrors how premake globs them, so the Linux build follows the code without
a second list to maintain by hand, and splitting by preprocessor in place keeps one feature in
one file, as 0550 asks. Growing the set later is one line per folder.

## Consequences

### Positive

- 714 of Core's doctest cases run in WSL from a Windows build.
- The helper (stage 2) links a library that is already tested on Linux.

### Negative

- A Windows-only `.cpp` added to a walked folder breaks the Linux build until it is excluded
  or ported; a developer who never runs the Linux build learns of it from whoever does next.
- clang flags things MSVC does not, so the Linux build is one more build to keep clean.

### Neutral

- `Git/Command` (it starts `git.exe` through `ChildProcess`) and the directory watcher join
  when the helper needs them, with Linux versions of process launching and inotify.

## Alternatives Considered

- **A separate `CoreHeadless` folder or premake project with moved files.** Lost: it splits
  features across folders against 0550, and moves dozens of files for a build concern.
- **Hand-written file lists in `build.zig`.** Lost: every new Git file would need a second
  edit that nothing reminds anyone to make.

## Related

- [0550](0550-lay-out-sources-by-feature-in-whole-word-folders-mirrored-by-the-tests.md)
- [0693](0693-cross-compile-the-linux-parts-with-a-pinned-zig-fetched-by-a-script.md)
