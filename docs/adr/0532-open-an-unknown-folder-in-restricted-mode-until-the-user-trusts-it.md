# 0532. Open an unknown folder in restricted mode until the user trusts it

Status: proposed
Date: 2026-09-30

## Context

Graphite is heading for a public release as a full IDE. Build, run and debug,
the next stages of the roadmap, will execute code that a repository controls:
MSBuild targets and tasks, a server that loads projects, a debuggee. Opening a
cloned repository must not be enough to run any of that.

Nothing the application starts by itself executes repository code today, but
clangd and the background analysis already start unasked. They read
repository-controlled configuration (`.clangd`, `compile_commands.json`) and
write into the tree (`.cache/`, `.graphite/analysis`). The model that decides
whether a folder may do these things is cheapest to build now, so that build,
run and debug arrive with one question to ask rather than each inventing an
answer.

## Decision

- Keep a list of trusted folders in `Graphite::WorkspaceTrust`. A folder is
  trusted when it equals an entry or lies under one.
- Compare paths lexically: after `lexically_normal`, with separators unified,
  the trailing separator dropped and case folded. `D:\Repos2` is not under
  `D:\Repos`. A relative or empty path is never trusted.
- Persist the list in `%LOCALAPPDATA%\Graphite\trusted-folders.json`, resolved
  by `UserDirectories::TrustedFoldersFile()`. The file holds a version and an
  array of UTF-8 paths. Load it at startup and write it on every change,
  unless it was written by a newer version or exists but could not be opened.
- Fail closed. An unreadable file means an empty list. A file stamped with a
  newer version than the build's own is read but never written. A file that
  exists but cannot be opened, as when something holds it without sharing,
  trusts nothing for the session and is never written, so a passing lock does
  not wipe the list.
- Do not ask when a folder is opened. An unknown folder opens in restricted
  mode. A bar above the editor area offers "Trust Folder" and "Trust All in
  <parent>", the second absent when the parent is a drive root. File menu >
  Trust This Folder toggles the same entry and is checked and disabled when the
  folder is trusted through a parent. Settings > Trusted Folders lists every
  entry with a Remove button.
- In restricted mode editing, browsing, search, Git, the terminal and Claude
  sessions work as before. The language server and the background analysis do
  not start, and the status bar's server slot says "Restricted mode". When a
  document's own server is restricted, the detail names that server's root,
  since it can lie above the open folder or be another folder altogether.
- Gate the language server in `LanguageServices`, not in its callers. It takes
  a predicate over a server's root that defaults to permitting everything. A
  refused root gets an entry in `ServerState::Restricted` with no process,
  which keeps the documents sent to it. `Revalidate()` shuts down servers whose
  root the predicate now refuses and starts restricted ones it now permits,
  replaying the kept documents. Files outside the workspace resolve their own
  root and are judged by it, and a document's status is its own root's server,
  never another root's of the same language.
- Give every file a root it can be judged by, and keep a document on the
  server it was opened on for as long as it is open. A file with no marker
  above it is rooted at the open folder when it lies inside it (compared
  case-insensitively), else at the root of the server that named it in an
  answer, else at its own directory - never at an empty root, which no entry
  can name and which would put files from differently judged folders behind
  one answer. `LanguageServices::SetFallbackRoot` is set from the workspace
  open and close events, before the folder's tabs are restored. A server
  outside the open folder - neither containing it nor inside it - that has
  held no document and no pending ticket for two minutes is shut down.
- Treat `build/compile_commands.json` as a C++ project marker. clangd reads
  it under every ancestor of a file, so a directory holding one is the root
  the trust check has to judge. A subdirectory with its own `build/` is
  therefore a project of its own: it gets its own server, and the
  background analysis of the enclosing folder does not descend into it.
- Fold the analysis into the same switch: it runs only when its setting is on
  and the folder is trusted. `GraphiteLayer` computes trust once per frame and
  reacts to the edge. Falling: `Revalidate()`, cancel the walk, clear its
  queue, detach the store. Rising: `WarmUp`, and attach the store and start
  the walk if the setting is on.
- Build, run and debug must ask `WorkspaceTrust` before they start anything,
  and refuse for a folder it does not trust.

## Rationale

A single list behind one question gives every present and future consumer the
same answer, and makes trusting and untrusting take effect on the next frame
without reopening anything. Placing the gate in `LanguageServices` covers files
that lie outside the workspace, which a switch kept in the layer that opens the
workspace would miss. A bar instead of a prompt keeps opening a folder
frictionless and keeps the choice visible and reversible.

## Consequences

### Positive

- Opening a cloned repository starts no process that reads the repository's
  configuration, and writes no index or analysis output into it. Graphite's own
  view state in `.graphite/workspace.json` and in-process Git reads continue.
  Later stages have one place to ask.
- Trusting and revoking act at once, with no restart and no reopening of tabs.
- Trust is granted by the user's action in the interface, never by the contents
  of the folder being opened.

### Negative

- Every folder a user already works in is restricted after the update until it
  is trusted once.
- A restricted C++ file shows no colours and no lenses, and go-to-definition
  does not answer.
- Matching is lexical, so trust follows the spelling of a path, not the place
  it names. A junction or symlink inside a trusted folder is trusted, although
  it may lead anywhere. A folder reached through a different spelling of the
  same place (a junction, a `\\?\` prefix, a mapped drive against a UNC path)
  is judged by the spelling used, and may be trusted or restricted differently
  from the same folder opened another way.
- The list is per machine. Trust granted on one machine is not carried to
  another, and is asked for again there.
- An unreadable file trusts nothing, so a corrupted list silently restricts
  every folder until the user trusts them again.
- A build older than the file's version leaves it alone, so trust granted in
  that build is lost when the list is next written by a newer one.

### Neutral

- The list sits beside the other machine-local state and apart from
  `graphite.json`, so preferences can be copied between machines without
  carrying trust.
- The terminal and Claude sessions are not gated: both start only on the
  user's command.
- There is no way yet to trust a single file's root from the interface when no
  folder is open. Such a file gets a server only when its root - its project,
  or its own directory - already lies under a trusted entry; otherwise its
  folder has to be opened and trusted. A header reached by go-to-definition is
  served by the project that named it, so it needs no entry of its own. The settings section lists and removes entries and
  does not add them.

## Alternatives Considered

- **A prompt when a folder is opened.** It forces a decision before the user
  has seen the folder, and the user chose a bar that can be answered later
  and revisited.
- **A single on/off switch in `LanguageServices`.** It is simpler, but it
  judges the workspace and not the root, so a file outside the workspace would
  start a server for a root nobody had trusted.
- **A gate in `Core::ChildProcess`.** Every process would pass through one
  place, but Core has no notion of a workspace and cannot judge trust, and the
  gate would also block the terminal, which the user starts on purpose.
- **Resolve paths to their final target before matching.** It would close the
  spelling differences, at the price of filesystem access in every trust
  check and of results that change when a link is retargeted. Lexical matching
  is predictable and is stated as a limit above.

## Related

- [0531](0531-keep-per-user-state-in-the-user-s-profile-not-beside-the-executable.md):
  where the per-machine list lives and why an older build never writes a newer
  file.
- [0002](0002-keep-core-free-of-ide-knowledge.md): why the gate is not in Core.
- [0488](0488-ship-clangd-with-the-release.md): the language server the gate
  holds back.
