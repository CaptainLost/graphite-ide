# 0531. Keep per-user state in the user's profile, not beside the executable

Status: proposed
Date: 2026-09-30

## Context

The application wrote four kinds of state into its own directory:

- `graphite.json`, the settings, which also carry the window placement and the
  last folder;
- `imgui.ini`, the window and docking layout, written by Core;
- `themes/`, where the settings panel saves the user's own colour schemes;
- `sessions-cache.json`, the cache behind the Claude sessions panel.

That holds while the executable runs out of a build directory. It breaks once
the application is installed. Under `Program Files` a normal user cannot write,
and Windows either refuses the write or quietly redirects it into VirtualStore,
where the user never finds it. One executable is also shared by everyone who
runs it, so two users on one machine would share one set of settings.

Each build configuration also had its own directory, so Debug, Release and Dist
each kept a different set of settings.

## Decision

- Keep what follows the user between machines under `%APPDATA%\Graphite`:
  `graphite.json`, `imgui.ini` and `themes/`.
- Keep what belongs to one machine under `%LOCALAPPDATA%\Graphite`:
  `sessions-cache.json` and `logs/`.
- Keep only what ships with the application beside the executable: fonts and
  the language server. Nothing beside it is ever written at run time.
- Resolve every one of these paths in `Graphite::UserDirectories` and nowhere
  else. Core asks the platform for the folders (`Core::RoamingDataDirectory`,
  `Core::LocalDataDirectory`) and is handed the layout file through
  `ApplicationSpec::LayoutFile`, so it never learns the application's name.
- When Windows cannot name a folder, fall back to the executable's directory
  and log a warning, rather than refusing to start.
- Stamp `graphite.json` with `settingsVersion`, and never write a file whose
  version is newer than the build's own.
- Do not ship colour schemes as files. The built-in schemes, dark and light,
  live in the code; any other scheme is the user's own.
- Move no one's existing files automatically. Nobody outside development has
  any yet, and a one-off manual copy costs less than code kept afterwards.
- Point the test runner at a directory in its build output
  (`UserDirectories::Redirect`) before any test runs, and refuse to run when it
  cannot, so a test that exercises a setter never writes the developer's real
  settings.

## Rationale

These are the folders Windows provides for exactly this purpose. Writing
anywhere else is what makes an installed copy misbehave. Resolving every path
in one class keeps a later move, such as a portable mode, to a change in one
file. The version stamp costs one key now. Without it, an older build would
write a newer file back in the old format.

## Consequences

### Positive

- An installed copy works for a normal user, and every user of a machine has
  settings of their own.
- Debug, Release and Dist builds share one set of settings and one layout.
- Settings, layout and schemes follow a roaming profile to another machine.

### Negative

- The settings are no longer next to the build output, where a developer
  looked for them to edit or delete by hand; they are under `%APPDATA%`.
- Every build configuration now reads and writes the same files. A layout
  saved by a Debug session is the one a Dist session opens with.
- Roaming carries the window placement too, since it lives in
  `graphite.json`. On a machine with other monitors the window may first open
  where the other machine left it.
- The test runner writes a `user-data` directory into its build output.

### Neutral

- `.graphite/` inside a workspace is unchanged: it belongs to the workspace, not
  to the user, and carries no version stamp yet.

## Alternatives Considered

- **Keep everything beside the executable.** It is simplest and portable, and
  it does not work under `Program Files`, which rules it out for a released
  application.
- **A portable mode chosen by a marker file beside the executable.** Useful
  for a copy run from a USB stick. Nobody needs it yet, and `UserDirectories`
  is the one place it would go.
- **Copy the old files over on first run.** It would move a developer's own
  settings automatically, but the code would stay after the only move it was
  written for. A manual copy did the same job once.
- **Ship the gruvbox schemes read-only beside the executable, merged with the
  user's.** This makes the scheme list read two directories and turns editing
  a shipped scheme into copying it first. The gruvbox schemes are one user's
  own, not part of the application.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md): why Core is told the layout
  file rather than choosing it.
- [0488](0488-ship-clangd-with-the-release.md): the language server that still
  ships beside the executable.
