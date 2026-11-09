# 0688. Refuse to watch a folder inside WSL through Windows

Status: proposed
Date: 2026-11-09

## Context

A folder inside a WSL 2 distro is reachable from Windows through its share,
`\\wsl.localhost\<distro>\…` or `\\wsl$\<distro>\…`. A probe on 2026-11-08 (Ubuntu on WSL 2)
watched `\\wsl.localhost\Ubuntu\tmp\…` with `ReadDirectoryChangesW` (through
`System.IO.FileSystemWatcher`): the watch armed without an error, and then reported nothing
— not for a file written from Linux, not for one written from Windows through the share.

`DirectoryWatcher::Start` only fails when the directory cannot be opened, so on such a
folder it succeeded, the Workspace believed it was watching, and nothing outside Graphite
ever refreshed the tree or the Git window, with no word to the user. The repositories users
keep in WSL can be huge, and every `stat` through the share is a round trip into the VM.

## Decision

`DirectoryWatcher::Start` refuses any path `Core::ParseWslSharePath` recognises with
`ErrorCode::Unsupported` and the message "changes made inside WSL are not reported to
Windows, so '<path>' cannot be watched", before touching the disk. Callers treat it as any
failed start; the Workspace shows its "Not watching for changes" warning.

## Rationale

A refusal the user can see beats a watch that silently never fires. Polling instead would
walk a possibly huge tree over the share on every pass, which costs more than the feature
is worth; watching a WSL folder properly needs inotify inside the distro, which the WSL
support design gives to a helper running there.

## Consequences

### Positive

- A folder opened through a WSL share says it is not watched instead of going stale.
- No file-system call is made on the share to find this out.

### Negative

- Changes made outside Graphite in such a folder appear only after Refresh until the WSL
  helper exists.
- If a future WSL release delivers notifications through the share, this refusal hides a
  watch that would have worked; the probe has to be repeated to lift it.

### Neutral

- Ordinary SMB shares are not affected; only the `wsl.localhost` and `wsl$` hosts are.

## Alternatives Considered

- **Poll the tree when the folder is on a WSL share.** Lost: a huge repository walked over
  9P on every pass, the cost the WSL design rules out.
- **Keep watching and hope.** Lost: the probe shows it never fires, and the user is told
  nothing.

## Related

- `docs/superpowers/specs/2026-11-08-wsl-support-design.md` (stage 0; stage 2 replaces this
  with inotify in the distro).
