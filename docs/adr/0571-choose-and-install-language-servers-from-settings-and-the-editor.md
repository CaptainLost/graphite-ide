# 0571. Choose and install language servers from Settings and the editor

Status: proposed
Date: 2026-10-11

## Context

[0570](0570-run-a-language-server-from-graphite-s-install-once-its-runtime-is-found.md) runs the chosen server from Graphite's install. The choice still lived only in `graphite.json` and took effect after a restart. Nothing on screen said what a language was missing, and one command was the only way to install a server. [0569](0569-install-a-language-server-on-request-from-its-own-release-channel.md)'s installer reported every step, and nothing showed them.

## Decision

Settings has a Languages section with a tab for each language that has servers. A tab shows:
- the server list, with None first and the recommended server marked;
- the chosen server's description and the runtime it needs;
- its install state, with Install, Cancel, Check for updates, Update and Uninstall as the state allows;
- a custom path and extra arguments.

A change to the choice, the path or the arguments reconfigures the language at once. A cleared path or argument list is removed from `graphite.json` rather than stored empty. clangd's path under the old `languageServerBinary` key shows in the C++ tab, and the first edit there moves it.

A bar above the editor offers what the shown document's language is missing: a choice, an install, or a .NET runtime, whose download page opens in the browser. Only one bar shows at a time, and restricted mode wins. Not now hides a language's bar until Graphite restarts. While the chosen server installs, the bar shows the install's progress and offers Cancel.

The status bar names the language and the server, as in `C#: Roslyn ready`. It says when the active language's server is being installed or removed. Clicking it opens that language's tab.

NotFound means only a configured path that is wrong. A server found nowhere is NotInstalled.

What is installed is read once, on the install queue after the start-up sweep. From then on it follows this window's own outcomes. The install command, the bar and the tab offer an install from that record, never from the server's state.

An uninstall runs in four steps:
1. The uninstall job starts.
2. The servers running that server are stopped, and they stay stopped while the job runs.
3. The job retries removing the version for about a second while the stopped processes exit.
4. However the job ends, those servers are reconfigured.

An install or an uninstall restarts only the languages whose chosen server it is.

## Rationale

**At once.** A choice that needs a restart reads as a choice that did not work.

**From the record, not the state.** An untrusted folder holds every server at Restricted, but installing is allowed there. It is an action in Settings, not something the folder triggers.

**Stop, then hold.** A running server holds its files. If the next frame started the server again from the files being removed, the uninstall would fail. The job's retries cover the moment a stopped process takes to exit.

**Only the language that runs it.** Restarting Roslyn because csharp-ls was installed would reload a solution for nothing.

## Consequences

### Positive

- C# can be set up and changed without editing `graphite.json` or restarting.
- A missing server, runtime or choice is named where the file is read, with the step that fixes it.
- An install's progress is on screen in the tab, the bar and the status bar.

### Negative

- This window learns of another window's install or uninstall only at its next start.
- While an uninstall runs, the language's open files have no server, and the language reads as not installed.
- A cancel asked for during the version lookup takes effect when the lookup ends.
- An authenticating proxy (HTTP 407) has not been tried. Its refusal is named, and Settings and the toast show it.

### Neutral

- The status bar's words changed for C++ too: `C++: clangd ready` where it said `clangd: ready`.
