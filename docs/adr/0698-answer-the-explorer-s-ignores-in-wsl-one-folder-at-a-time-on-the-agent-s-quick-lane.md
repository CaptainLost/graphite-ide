# 0698. Answer the Explorer's ignores in WSL one folder at a time on the agent's quick lane

Status: proposed
Date: 2026-11-10

## Context

The Explorer dims what git ignores. `Core::FileTree` asks one entry at a time, synchronously, on
the UI thread, as it lists a folder; the Workspace answered from a `Core::Repository` it opened
itself. On a folder inside WSL that repository was opened by libgit2 over the share
(`\\wsl.localhost\…`): libgit2 refuses it for its owner ("git refuses…" in the log) and, where it
does not, each answer costs share round trips. Every other git question about a WSL repository
already goes to the distro's agent (ADR 0696), whose git requests run one at a time on one
worker — so a status of a huge tree can hold the worker for seconds, and an ignore question
queued behind it would freeze the Explorer that long.

## Decision

Ask the Explorer's ignore questions of the distro's agent, behind a `RepositoryIgnores` seam in
the Workspace (libgit2 here for a local folder, `WslIgnores` for a WSL one):

- The agent answers `git.ignoreSetup` (working directory, rule files outside the folder) and
  `git.ignored` (one folder's entries, in order) on a second worker, the quick lane, with handles
  of its own; no git request queues there, and a watch's first walk of the tree runs on a worker
  of its own too, so the agent's loop keeps reading requests and sending replies meanwhile.
- `Core::FileTree` hands a folder's listing over before it asks about its entries one by one;
  the folder is asked about whole, in one request. The answers are kept until the tree asks
  everything again; a name they do not list (made since), or list as the other kind, asks the
  folder again; a folder that could not be answered stays "not ignored" until then, logged once.
- A question waits at most 2 s. Once one runs out of time, nothing more is asked until the tree
  asks everything again, so a wedged agent costs one wait per refresh, not one per folder.
- Nothing on the UI thread waits for an agent to start. Opening answers Cancelled while the
  distro's agent is still connecting, and the Workspace opens again every 250 ms until it
  answers, then asks the whole tree again. A distro whose agent could not start answers that
  failure for 30 s rather than being started anew on every retry.

## Rationale

A folder's entries are listed together, so one request per folder costs one round trip where
per-entry questions would cost hundreds. A lane of its own keeps the Explorer's questions out of
the queue a long status holds. Not waiting at open keeps the window responsive while WSL boots.

## Consequences

### Positive

- No libgit2 handle is opened over the share; the Explorer of a WSL folder dims what git
  ignores as it does locally.
- A status of a huge tree never delays drawing a folder.

### Negative

- A folder opened before its distro's agent is up shows its ignored entries undimmed until the
  agent answers (0.6–1.6 s measured), then dims them in place.
- Each question still runs on the UI thread: a wedged agent can hold one frame for up to 2 s per
  refresh, after which the rest of the tree is answered "not ignored" until the next refresh.
- The agent's loop still walks a folder made inside a watched tree (to report what it holds)
  before it reads the next request: a burst such as an install can delay a question by as long.
- The agent holds a second set of repository handles.

### Neutral

- The rule files outside the folder are asked once, when the repository is opened, as the
  Workspace only asks for the open folder's.

## Alternatives Considered

- **Ask per entry, as `FileTree` does.** Hundreds of round trips per folder on the UI thread;
  lost on cost.
- **Ask on the git worker the other requests use.** The simplest, but an ignore question would
  wait behind a status of a huge tree, freezing the Explorer; lost.
- **Answer from the host by parsing `.gitignore` files over the share.** Would have to
  reimplement git's rules (global excludes, `info/exclude`, negation, directory rules) and read
  rule files over the share; lost to asking the git that owns them.

## Related

- [0696](0696-name-a-folder-inside-wsl-by-its-wsl-localhost-path-and-reach-it-through-one-agent-per-distro.md) — one agent per distro.
- [0695](0695-talk-to-the-wsl-agent-in-length-prefixed-frames-of-a-json-header-and-raw-bytes.md) — the protocol.
- [0688](0688-refuse-to-watch-a-folder-inside-wsl-through-windows.md) — why the share is not watched.
