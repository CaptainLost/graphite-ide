# clangd

This directory holds `clangd`, the C++ language server this application launches
as a subprocess (see
[ADR 0485](../../../docs/adr/0485-take-code-intelligence-from-external-language-servers.md)
and
[ADR 0488](../../../docs/adr/0488-ship-clangd-with-the-release.md)).

Nothing here but this file is committed. At roughly ninety megabytes the server
does not belong in source control, and `.gitignore` excludes everything in this
directory except this README so it cannot be added by accident — a binary that
size cannot practically be taken back out of the history again.

## Getting it

Download a pre-built Windows release from
https://github.com/clangd/clangd/releases — the `clangd-windows-<version>.zip`
asset, about 28 MB — and extract its **whole tree** here, so that this directory
contains:

```
vendor/bin/clangd/
    bin/clangd.exe
    lib/clang/<major>/include/     ~300 headers
    README.md
```

**Extract both directories, not just the executable.** `clangd` locates its
built-in compiler headers — `stddef.h`, `stdarg.h`, and the rest that every
translation unit needs before it reaches a line of your code — relative to its
own location, as `../lib/clang/<major>/include`. An executable copied away from
that tree still starts, still answers, and reports errors in every file it is
asked about, which reads as a broken project rather than a broken install.

Building the server from source is possible and not worth it: it means the whole
`llvm-project` monorepo, a second build system, hours of compilation and tens of
gigabytes of build tree, to produce a binary identical to the release above.

## Pointing the application at it

Set `languageServerBinary` in `graphite.json` — which lives beside the built
`Graphite.exe`, not in the repository — to the full path of `bin/clangd.exe`:

```json
"languageServerBinary": "D:\\Repos\\IDE\\vendor\\bin\\clangd\\bin\\clangd.exe"
```

`FindServerBinary` tries that setting first, then a copy beside the application,
then `PATH`. With none of them the status bar reports `clangd: not found` and the
editor works exactly as it did before this feature existed.

## Known gap

`Graphite/premake5.lua`'s postbuild step copies `clangd.exe` alone next to the
built application. That predates knowing about the `lib/clang` tree above, so a
binary placed by it would be severed from its built-in headers. Until that is
corrected, use the settings key rather than relying on the copy.
