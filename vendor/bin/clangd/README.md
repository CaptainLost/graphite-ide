# clangd

This directory holds `clangd.exe`, the C++ language server this application
launches as a subprocess (see
[ADR 0485](../../../docs/adr/0485-take-code-intelligence-from-external-language-servers.md)
and
[ADR 0488](../../../docs/adr/0488-ship-clangd-with-the-release.md)).

The binary is not committed to this repository — at roughly a hundred
megabytes it does not belong in source control, and it is downloaded
pre-built rather than compiled from source, unlike the submodules under
`vendor/`. Get it from the LLVM project's releases
(https://github.com/llvm/llvm-project/releases), which publish a Windows
`clangd.exe` for each LLVM version, and place it here as `clangd.exe`.

`Graphite/premake5.lua`'s postbuild step copies this file next to the built
application when it is present, and silently skips the copy when it is not —
a missing binary is not a build failure. Without it, `FindServerBinary` falls
through to a configured settings path or a copy on `PATH`, and the status bar
reports `clangd: not found` if neither exists either.
