# 0570. Run a language server from Graphite's install once its runtime is found

Status: proposed
Date: 2026-10-11

## Context

[0569](0569-install-a-language-server-on-request-from-its-own-release-channel.md) installs a server into `servers/<id>/<version>/` and records its command in `installed.json`. Nothing started a server from there yet, and C# had no server: two of its three servers run on .NET, and Roslyn loads no project until it is told which solution to open. A choice made per language ([0567](0567-keep-server-specific-behaviour-in-adapters.md)) also means a language may have no server chosen while its files are open.

## Decision

A server's command is looked for in one order, and the first match is the one that runs:
1. the configured path. When one is configured and does not exist, the server reads NotFound and names the path;
2. Graphite's install;
3. beside the executable;
4. PATH.

Nothing found reads NotInstalled. A configured path that is wrong is the only NotFound, so the status can say so ([0571](0571-choose-and-install-language-servers-from-settings-and-the-editor.md)).

A row names the runtime it needs. A .NET server needs a `dotnet` with a runtime of at least that major version. Installations are looked for in `DOTNET_ROOT`, beside `dotnet` on PATH and under Program Files, and versions are read from the directory names under `shared/Microsoft.NETCore.App`. Without such a runtime the server reads MissingRuntime with the version and does not start. A tool whose record names the `dotnet` runner starts as `dotnet <assembly>`.

A running server holds its installed command open, sharing only read access, so an update or an uninstall cannot remove the directory under it. A child process is started inside a job object that is killed as a whole when the server is ended or Graphite exits, because Roslyn's executable is a launcher with a child of its own.

C# lists Roslyn (recommended), csharp-ls and OmniSharp, and runs none until one is chosen. Until [0577](0577-install-clangd-on-request-like-every-other-language-server.md) C++ kept clangd when nothing was chosen. A document opened while its language has no server chosen is kept with its latest text, and is opened on the server chosen later.

Roslyn's adapter sends `solution/open` with the solution in use after `initialized`. A root with no solution but with `.csproj` files directly in it is sent `project/open` with all of them instead; they are listed on the UI thread before the client starts. The adapter reports "no project" when the root has neither, stays indexing until `workspace/projectInitializationComplete`, which Roslyn sends after either request, and is restarted when the Explorer selects another solution. The solution in use is the selected one when it lies inside the server's root, and otherwise the root's first solution by name, the `.slnx` when a `.sln` shares its name.

## Rationale

**The configured path wins outright.** Running another copy when the configured one is gone would answer with a different server than the one the user named, and hide why.

**Checking the runtime first.** A .NET server started without its runtime exits at once. The restart budget would then spend itself on a cause no restart can fix, and the status would say "failed" instead of what is missing.

**The job object.** Without it, ending Roslyn's launcher leaves its child holding the pipes. The client's reader then never sees the end of its input, and its destructor waits on it.

## Consequences

### Positive

- C# is served by the server the user chose, installed by Graphite or found on the machine.
- A missing runtime or a stale configured path is named in the status bar rather than seen as a failure.
- No server process outlives Graphite.

### Negative

- A C# folder runs no server until the user chooses one.
- A runtime of a newer major version counts as enough, though a server whose runtime does not roll forward across majors would still fail on it.
- Selecting another solution restarts every server of a language whose server follows it, including servers of other roots.
- `installed.json` is read on the UI thread once per server start. Settings reads what is installed off the UI thread ([0571](0571-choose-and-install-language-servers-from-settings-and-the-editor.md)).
- A stopped server's files stay locked for a moment after it is ended, so removing its version right after a stop can fail and must wait or retry.
- Where Graphite installs servers, a C++ machine with no clangd now reads 'not installed' and is offered the install, ahead of stage 5.

### Neutral

- csharp-ls and OmniSharp find the solution in their root by themselves and keep the plain adapter.
