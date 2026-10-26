# 0641. Inherit the ignored state from an ignored folder and keep known answers across reloads

Status: proposed
Date: 2026-10-26

## Context

The Explorer marks ignored files by asking the repository for every entry it
lists. Each answer cost about 95 µs, and about half of that was
`std::filesystem::relative`, which touches the disk to build a path that string
arithmetic can build. The cost showed in two places:

- **Expanding a folder.** 321 entries of ignored build output took 32 ms, against
  1 ms without the questions.
- **Reloading the tree.** Graphite reloads the tree when it comes back to the
  front after two seconds away. The reload asked again for every node of every
  expanded folder: 2,020 nodes took 197 ms, against 10 ms without the questions.

## Decision

- **Build the repository-relative path lexically**, from normalised absolute
  paths, never with `std::filesystem::relative`.
- **Ask about no entry inside an ignored folder.** Its children are ignored,
  because git cannot un-ignore a path inside an ignored directory.
- **Keep the answers a reload already has.** A reload asks only about nodes it
  did not know before.
- **Ask about everything again when an ignore source may have changed:** a
  `.gitignore`, `.git/info/exclude` or the repository's config. If that signal
  cannot be routed reliably, the automatic reload keeps the answers and an
  explicit Refresh asks again.

## Rationale

The answers change only when an ignore rule changes. Asking again on every
reload pays for every node to find out what was already known.

## Consequences

### Positive

- Expanding an ignored build folder and reloading a large expanded tree become
  cheap.

### Negative

- A rule changed outside the signals named here leaves a mark stale until an
  explicit Refresh.

### Neutral

- What is shown as ignored is unchanged.

## Alternatives Considered

- **Ask libgit2 for the whole folder at once.** libgit2 has no batch ignore
  query.
- **Read the status of ignored files.** Ignored files are excluded from the
  status on purpose, because they are many.

## Related

- [0640](0640-read-a-folder-s-entries-and-their-attributes-in-one-pass.md)
