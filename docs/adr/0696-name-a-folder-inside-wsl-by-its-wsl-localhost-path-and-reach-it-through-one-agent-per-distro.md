# 0696. Name a folder inside WSL by its wsl.localhost path and reach it through one agent per distro

Status: proposed
Date: 2026-11-10

## Context

Graphite must work on folders inside a WSL 2 distro as it does on local ones, on repositories
that may be huge (the WSL support design). The host names files by `std::filesystem::path`
throughout: 186 of Graphite's headers use it as identity, and the code converts paths to text
in 345 places. The design asks for a location that is not a UTF-8 Windows path.

Measured on 2026-11-09 through `\\wsl.localhost\Ubuntu\…`: a `stat` costs about 0.8 ms,
reading a small file about 1.4 ms, listing 2000 entries 5–14 ms, and reading 2.7 MB 67 ms.
`ReadDirectoryChangesW` arms but reports nothing (ADR 0688), and `ReplaceFileW` fails with
error 50, so every save into such a folder failed; a Windows-side rename would also drop
Linux permission bits. Stage 2a built `graphite-agent`, which runs in the distro and writes,
renames, trashes and watches with inotify (ADR 0695).

## Decision

On the host a place in WSL is spelled `\\wsl.localhost\<distro>\<linux path>` with
backslashes — one spelling: `\\wsl$\`, any case of the host name, `\\?\UNC\` and forward
slashes are converted to it on entry (`Core::CanonicalWslSharePath`). That spelling is the
location: `Core::ParseWslSharePath` turns it into `{distro, linux path}` and
`Core::ToWslSharePath` back. Path keys of such places keep their case, as Linux does; every
other path folds case as before.

Reads and per-folder listing stay on the share. Writes and Explorer operations (save, rename,
create, delete to the Linux trash) go through `Core::WslFileOperations`, which Core's
`WriteFileAtomically`, `Rename`, `MakeDirectory`, `CreateEmptyFile` and `MoveToRecycleBin`
consult for share paths; watching goes through `Core::FolderWatch`. Graphite implements both
with one agent per distro, started on demand.

Deployment: the agent binary ships beside `Graphite.exe`; it is copied through the share to
`~/.graphite/agent/<first 16 hex of its SHA-256>/graphite-agent` (written as `.part`, then
`chmod` and `mv` inside the distro), counts as installed when a file of the same size is
there, is started with `wsl.exe -d <distro> --exec`, reports the name of its folder as its
version, and removes the other version folders beside its own.

## Rationale

The UNC spelling round-trips exactly to `{distro, linux path}`, so it serves as the location
without touching every header that holds a path; Windows Explorer opens it, and a single read
through it is as fast as asking an agent would be. Only what the share cannot do — writes,
renames that keep Linux metadata, the trash, watching — needs the agent, so the agent stays
small and the folder lists at once while it starts.

## Consequences

### Positive

- A WSL folder lists and opens files immediately; saving, watching and the trash work once
  the agent is up.
- Local folders and ordinary shares are untouched.

### Negative

- A name the share cannot spell (bytes that are not UTF-8, or characters Windows forbids,
  which WSL maps to private-use code points) is reached through that mapping, not as Linux
  names it.
- Code that reads a WSL path with a raw `std::filesystem` call still goes through 9P; bulk
  work (git, search) must move to the agent in later stages, as the design says.
- Another remote kind (SSH) would need a spelling of its own; this one is WSL's.

### Neutral

- A save on the UI thread waits for the agent: up to 30 s for it to start, then up to 30 s
  for its reply, with no indicator yet. The agent is started when the folder opens, so it is
  usually ready (0.6 s on a warm VM) by the first save.
- A distro's name is folded to lower case in path keys and in the agents' map, as WSL takes it
  in any case; the names inside it keep theirs.

## Alternatives Considered

- **A location type in place of `std::filesystem::path` everywhere.** Lost: hundreds of
  signatures and conversions changed for the same information the UNC spelling carries.
- **Every read through the agent too.** Lost: no faster than the share for single files, and
  the folder could not list until the agent had started.

## Related

- [0688](0688-refuse-to-watch-a-folder-inside-wsl-through-windows.md)
- [0693](0693-cross-compile-the-linux-parts-with-a-pinned-zig-fetched-by-a-script.md)
- [0695](0695-talk-to-the-wsl-agent-in-length-prefixed-frames-of-a-json-header-and-raw-bytes.md)
