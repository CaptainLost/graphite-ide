# 0660. Carry a folder's trust with it when it moves while open

Status: proposed
Date: 2026-11-03

## Context

Trust is kept as a list of folder paths (0532). The watcher reports when the
open folder itself is renamed or moved, and the application follows it: the
documents, the saved state, the reviews, the git port and the language servers'
fallback root all move to the new path. Trust did not. A folder the user had
trusted reopened its bar of restricted mode the moment it was renamed, with
nothing inside it changed, and the old path stayed trusted for whatever might
appear there later.

## Decision

- When the open folder moves, call `WorkspaceTrust::FollowFolderMove(from, to)`
  before anything else in the move handler, since the documents moved after it
  ask trust whether their server may run.
- Rewrite every entry that is the moved folder or lies under it to the same
  place under the new path, and save the list.
- Give a folder that was trusted only through a parent it has now left an entry
  of its own, so it stays trusted.
- Leave an untrusted folder untrusted. One moved under a trusted parent is
  trusted through that parent, like any folder there.

## Rationale

The moved folder holds the same files the user agreed to trust, so asking again
protects nothing. Rewriting the entry rather than adding one keeps the old path
from trusting a folder that is created there afterwards.

## Consequences

### Positive

- Renaming the open folder keeps its language server and analysis running.
- The trust list never names a path that only existed before a move.

### Negative

- A move made while Graphite is closed is not seen: the folder opens restricted
  at its new path and has to be trusted again.
- A folder that leaves the parent it was trusted through adds a narrower entry,
  so the list grows by one.

### Neutral

- Settings > Trusted Folders shows the rewritten entry on its next draw, through
  the revision the list already bumps on every change.

## Alternatives Considered

- **Keep trust on the path.** The renamed folder opens restricted. Lost: it asks
  the user to trust again a folder nothing has changed in, and leaves the old
  path trusted.
- **Add the new path and keep the old entry.** Lost: the old path stays trusted
  for whatever is created there next.

## Related

- [0532](0532-open-an-unknown-folder-in-restricted-mode-until-the-user-trusts-it.md)
  sets up the trust list this record moves.
