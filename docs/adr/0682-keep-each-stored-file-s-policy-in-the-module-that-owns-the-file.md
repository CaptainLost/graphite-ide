# 0682. Keep each stored file's policy in the module that owns the file

Status: rejected
Date: 2026-11-08

## Context

The architecture review of 2026-11-08 found the same life of a stored file in
many places: read, check the version, copy a file that cannot be taken in aside
before writing over it, wait two seconds after the last change, write
atomically. `ReviewStore`, `WorkspaceTrust`, `AppSettings`, `WorkspaceSettings`,
`SummaryCache`, `StateKeeper` and the git stores (`LineStore`, `ContentStore`,
`GraphFile`, `PatchIdFile`) each do part of it. One "stored file" module owning
the path, the codec, the version rule, the copy aside and the delayed save was
proposed.

## Decision

Do not write that module. Each store keeps its own rule and calls the shared
helpers it already uses: `Core::WriteFileAtomically`, `BackUpBeforeWritingOver`
and `StoredVersionOf`.

## Rationale

The steps are already shared; what differs is the rule, and the rules differ on
purpose:

- the settings read the file again when they write, so an edit made in another
  editor is not lost;
- trust never writes over a file of a newer version, since that would widen or
  narrow what a newer build trusts;
- the reviews leave a file alone when they could not tell whether it exists, and
  copy a file of another version aside before the first write;
- the summary cache drops a file of another version, since it can be rebuilt;
- the git stores rebuild from the repository and keep their own binary formats.

A module that held them all would take each rule as a parameter, so its
interface would be as wide as what it hid, and every store would still state its
rule. The shared part each store writes is three to six lines.

## Consequences

### Positive

- A store's rule stays beside the data it protects, where its tests are.

### Negative

- The two-second delay is written twice, in `ReviewStore` and `StateKeeper`.
- A new store has to pick its rule again rather than inherit one.

### Neutral

- If several new stores turn out to share one rule exactly, that rule becomes the
  module to write.

## Alternatives Considered

- **One delayed-save helper for `ReviewStore` and `StateKeeper`.** Two users with
  different triggers (a version counter, a captured state compared each frame);
  the shared part is a timer.

## Related

- [0681](0681-keep-repositorygitport-s-forwarding-bodies-thread-local-handles-and-one-gitport.md)
  is the other candidate of the same review that was turned down.
