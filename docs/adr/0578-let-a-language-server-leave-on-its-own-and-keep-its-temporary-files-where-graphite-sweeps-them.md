# 0578. Let a language server leave on its own and keep its temporary files where Graphite sweeps them

Status: proposed
Date: 2026-10-12

## Context

clangd runs with `--pch-storage=disk`, so the preamble of every open file is a
file of tens of megabytes in the system's temporary directory. clangd deletes
it when it exits on its own. A process that is ended never gets to.

Graphite sent `shutdown` and `exit` and then ended the process straight away,
from the UI thread, every time a server stopped:
- when Graphite closed;
- when a server sat idle outside the workspace;
- when its choice was reconfigured;
- when its folder lost trust.

The reasoning was that the UI thread must not wait on a server that may be
wedged, and that a server keeps nothing that cannot be rebuilt from the files
on disk. The first part holds. The second does not: one machine had collected
65 orphaned preambles in `%TEMP%`, about 2 GB.

Ending a process also remains unavoidable when Graphite itself crashes or is
stopped in a debugger. A server is in a job that is killed when Graphite goes.

## Decision

**Stop a server off the UI thread, and give it time to leave.** Every running
client that is let go goes through `LanguageServices::Retire`. That hands it
to a worker, which:
- sends `shutdown` and `exit`;
- waits up to `LspClient::kShutdownGrace` (2 s) for the process to exit;
- ends it only after that.

A server whose handshake never finished was never sent the two messages. It is
ended at once. When Graphite closes, the workers finish every retirement
before they are joined.

**Keep a server's temporary files where Graphite can find them.** Each
Graphite claims a numbered directory under
`%LOCALAPPDATA%\Graphite\server-temp`. It holds a lock file open inside it for
as long as it runs. Every server it starts gets `TMP` and `TEMP` pointing
there.

When the directory is set, Graphite sweeps every other directory under
`server-temp` it can rename. The rename refuses a directory whose lock is held,
so another running Graphite's directory stays. A crashed Graphite's lock file
is deleted with its process, so its directory goes.

## Rationale

Waiting is what lets the server clean up after itself. Doing it on a worker
keeps the rule that the UI thread never waits on a server. A directory of
Graphite's own is the only place a sweep can safely delete from: the system's
temporary directory is shared with every other program that runs clangd. The
held lock answers "is this directory's owner still running" without process
ids, which can be reused.

## Consequences

### Positive

- A server that leaves when asked leaves no temporary files behind. clangd does
  so in about half a second.
- Whatever a killed server leaves is removed the next time Graphite starts,
  crash or not.
- A wedged server still cannot hold up the UI thread or the close for longer
  than its grace.

### Negative

- Closing Graphite now waits for its servers to leave, about half a second with
  clangd. The window stays on screen meanwhile.
- A server lingers up to its grace after it is let go. An uninstall started at
  the same moment retries for about a second. If the server is still running
  after that, its version directory stays for the next start-up sweep.
- The workers that walk a folder for the warm-up and search it for projects also
  carry retirements, so a busy queue can delay a server's exit.
- Leftovers of a crash stay on disk until Graphite is next started.

### Neutral

- Servers on .NET, and any helper process a server starts, inherit the same
  `TMP` and `TEMP`.
- With `--user-data`, the redirected root has a `server-temp` of its own.
- A restart after a server has died still destroys the dead client at once.
  There is no process left to wait for.

## Alternatives Considered

- **Wait on the UI thread.** It is correct for a healthy server, and a wedged
  one freezes the editor for the whole grace, every time.
- **Keep ending servers at once and sweep `%TEMP%` for `preamble-*.pch`.** The
  files are not Graphite's alone. Another editor's clangd uses the same names,
  and a file deleted under a running server can break it.
- **Mark each directory with the owning process id.** Ids are reused, and
  telling a live owner from a dead one would need a process query that a held
  file answers for free.

## Related

- [0485](0485-take-code-intelligence-from-external-language-servers.md): servers
  run as external processes.
- [0569](0569-install-a-language-server-on-request-from-its-own-release-channel.md):
  the same rename-before-delete rule for a server's version directory, and the
  start-up sweep it relies on.
- [0268](0268-cp-26-dont-detach-a-thread.md): why retirement is a job on a joined
  queue, not a detached thread.
