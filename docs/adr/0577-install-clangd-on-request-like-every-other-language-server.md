# 0577. Install clangd on request like every other language server

Status: proposed
Date: 2026-10-12

## Context

[0488](0488-ship-clangd-with-the-release.md) ships clangd beside the executable. A post-build step copies it from `vendor/bin/clangd/` when a developer has put a binary there. The copy is the bare `clangd.exe`, without the `lib/clang` headers it needs, and on a machine without that folder nothing is shipped at all.

Since then, servers come from Graphite's own install:
- [0569](0569-install-a-language-server-on-request-from-its-own-release-channel.md) installs a server on request from its own release channel;
- [0570](0570-run-a-language-server-from-graphite-s-install-once-its-runtime-is-found.md) runs it from Graphite's install;
- [0571](0571-choose-and-install-language-servers-from-settings-and-the-editor.md) lets the user choose and install it from Settings and the editor.

C++ alone still started clangd when nothing was chosen, and the flat `languageServerBinary` key still named its path.

## Decision

Graphite no longer copies clangd beside its executable, and the repository keeps no place for it.

C++ starts with no server chosen, like every other language. Opening a C++ file shows "No C++ language support" and leads to Settings, where clangd is chosen and installed.

When the settings load, a `languageServerBinary` value is moved, once:
- it becomes `lsp.clangd.binary.path`, unless a path is already stored there;
- C++ is set to clangd, unless a choice is already stored;
- the old key is removed;
- a blank value is removed and chooses nothing.

A server is looked for at its configured path, then in Graphite's install, then on `PATH`. Graphite no longer looks beside its own executable.

## Rationale

**One way in.** What a language runs is what the user chose, from where Graphite put it or from where the user pointed. A server that appears because a file happens to sit in a folder is one nobody chose.

**Nothing beside the executable.** No one puts a server there any more. Old build folders still hold the bare `clangd.exe` the copy left behind, which starts and then reports errors in every file it is asked about.

## Consequences

### Positive

- clangd arrives whole, with its headers, from its own release.
- C++ and C# are set up the same way.
- Whoever set the old key keeps C++ working without doing anything.

### Negative

- Someone who relied on the copy beside the executable, and set nothing, meets the bar on their first C++ file and installs clangd.
- A developer working on this repository chooses and installs clangd, or points `lsp.clangd.binary.path` at a local copy.

### Neutral

- A clangd found on `PATH` still runs, and Settings says it comes from outside Graphite's install.
- `vendor/bin/clangd/` stays ignored by git, for developers who keep a local copy there.

## Alternatives Considered

- **Keep choosing clangd for C++ when nothing is chosen.** The first C++ file would then report a server that is not installed, without ever offering the choice the user has for every other language.
- **Choose clangd once for every settings file from an earlier version.** It saves one click for some users, at the price of a settings version that stops older builds writing the file.

## Related

- [0488](0488-ship-clangd-with-the-release.md): superseded by this record.
- [0569](0569-install-a-language-server-on-request-from-its-own-release-channel.md), [0570](0570-run-a-language-server-from-graphite-s-install-once-its-runtime-is-found.md), [0571](0571-choose-and-install-language-servers-from-settings-and-the-editor.md): the install and the choice this relies on.
