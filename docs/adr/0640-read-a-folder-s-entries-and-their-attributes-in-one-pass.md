# 0640. Read a folder's entries and their attributes in one pass

Status: proposed
Date: 2026-10-26

## Context

Expanding a folder in the Explorer runs `Core::ReadDirectory` on the UI thread.
It walked the folder with `std::filesystem::directory_iterator` and then asked
`GetFileAttributesW` once per entry to learn whether the entry is hidden. Measured
in Release:

| Folder | Walk only | With one attribute call per entry |
|---|---|---|
| C:\Windows\System32 (5,107 entries) | 1.8 ms | 107 ms |
| C:\Windows\WinSxS (29,814 entries) | 28 ms | 966 ms |

Almost all the time of expanding a large folder went into those calls. The
enumeration itself already returns every entry's attributes.

## Decision

On Windows, `Core::ReadDirectory` enumerates with
`FindFirstFileExW(FindExInfoBasic, FIND_FIRST_EX_LARGE_FETCH)` and takes the
hidden flag, the directory flag and the size from the data the enumeration
returns. It never queries an entry again. The entries, their order and their
flags stay as they were. Other platforms keep their own branch.

## Rationale

The attributes come back with the name at no extra cost. Asking for them again
is one system call per entry, and on a large folder that is a frozen window.

## Consequences

### Positive

- Expanding a folder of tens of thousands of entries no longer freezes the
  window for a second.

### Negative

- `ReadDirectory` has a Windows-specific enumeration beside the portable one,
  and both must agree on what an entry is.

### Neutral

- Reparse points and links are reported as the enumeration sees them, which is
  what the attribute query saw too.

## Alternatives Considered

- **Read the folder on a worker.** That leaves the work as it was and adds a
  loading row to every expand, for a cost that disappears once the redundant
  calls go.
- **Drop the hidden check.** Hidden files are shown dimmed on purpose.

## Related

- [0641](0641-inherit-the-ignored-state-from-an-ignored-folder-and-keep-known-answers-across-reloads.md):
  the other per-entry cost of an expand.
