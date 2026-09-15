# 0513. Keep the workspace analysis on disk and discard it on any doubt

Status: proposed
Date: 2026-09-15

## Context

The background pass of [0511](0511-analyse-the-workspace-ahead-of-the-reader-several-files-at-a-time.md)
spends minutes on a small repository and tens of minutes on a large one asking
a language server about every file it serves. `AnalysisCache` keeps what comes
back, but only in memory and only under a byte budget, so every answer the pass
bought was thrown away when the process exited. The second launch on an
unchanged folder redid all of it — the same walk, the same `didOpen`s, the same
minutes — to arrive at exactly the answers the first launch had already
computed.

Nothing else in this application writes derived data to disk. `.graphite/`
exists, and `WorkspaceSettings` already writes there under a `.gitignore` the
repository ships, but everything in it so far is something a person chose, not
something a machine computed and can compute again.

## Decision

Give the pass a second tier, `AnalysisStore`, under `.graphite/analysis` in the
workspace being analysed. One record per analysed file plus one index, written
by a worker and read on the UI thread.

- **The index exists so pruning is possible.** A record is named by a hash of
  its path, and a hash cannot be turned back into a path. Without a list of
  the paths, nothing could tell that a file had been deleted or renamed, and
  records for files that no longer exist would accumulate for the life of the
  project in a store that deliberately has no size cap. The index is read and
  pruned against the filesystem when a root is pointed at, and every record the
  pruned index does not name is swept — those are the unreachable ones, and
  walking the index cannot see them at all.
- **Discard on any doubt, silently.** A record or an index that is truncated, a
  version behind, for a different path, or stamped against a different version
  of the file is thrown away without a message. There is no repair path and no
  diagnostic, because the fallback is not a degraded mode: it is asking the
  server, which is what the caller would have done had the store been empty. A
  format bump throws the whole store away at once, by writing the record
  version into the index.
- **Read synchronously on the UI thread — the one deliberate exception.** It is
  a single small file, read in the frame a document opens, at a moment when
  `ProbeFile` and `ModifiedTimeOf` already touch the filesystem. Doing it on a
  worker would cost two frames and hand back the visible recolour the whole
  feature exists to remove.
- **Write on a worker, one thread.** Tens of kilobytes of overlay serialised
  inside a frame is a visible hitch, once per analysed file. The index is
  rewritten on that same thread and not with every record — it names every
  entry, so a rewrite per record makes the bytes written over a first pass
  quadratic in the size of the workspace. It is coalesced instead, to at most
  one rewrite per some tens of records or per some tens of seconds, and flushed
  when the root changes or the store is destroyed. One thread and not several
  either way: two writers would race to assemble the index from disagreeing
  pictures of the disk.
- **Both producers write both tiers.** The pass writes what it analysed and
  `DocumentWindows::KeepAnalysis` writes what a closing tab knew, into the cache
  and the store together. A store filled only by the pass would forget every
  file a reader opened before the walk reached it.

## Rationale

Persistence is worth a new subsystem rather than a flag on `AnalysisCache`
because the two classes differ in every dimension that matters: one is bounded
by bytes and the other by what is on the disk, one is exact and the other is
allowed to be wrong, one touches no filesystem and is tested without one and
the other is entirely about a filesystem. Folding the disk into the cache would
have put a corruption check, a worker and a directory layout inside a class
whose current value is that it has none of them.

Discarding on doubt rather than repairing is what makes the rest affordable to
reason about. The store holds nothing that cannot be recomputed, so every
failure has the same answer, and there is no state in which a caller must decide
how much of a damaged store to trust.

## Consequences

### Positive

- A second launch on an unchanged folder skips the work the first one paid for:
  no server is troubled and no worker reads the file, and the entry is read from
  the disk in the frame a document opens it.
- A corrupt, truncated or half-written store costs one session's worth of
  redone analysis and nothing else — the worst outcome is the behaviour that
  preceded this record.
- The decoder's own enumeration check means a kind read out of a damaged record
  cannot index past the end of `SyntaxPalette::Colours` at drawing time, which
  is the failure a binary format on disk invites.

### Negative

- A `.graphite/analysis` directory grows in every workspace that is analysed,
  with no size cap. Pruning bounds it by the number of files that still exist,
  not by bytes, so a repository of very large files has a correspondingly large
  store.
- File I/O on the UI thread, once per opened document, in direct tension with
  the rule the rest of this codebase keeps. It is argued in `AnalysisStore.h` at
  the one place it happens rather than left to be rediscovered, but the rule is
  now one with an exception in it.
- A closing tab's analysis is written to the store of whichever workspace is
  open, even for a file outside that workspace's tree. The record is keyed by
  its normalised absolute path, so it is never served for the wrong file; it is
  simply kept somewhere a reader would not look for it.
- The index is the very thing rejected under "One file for the whole store": it
  names every entry, so every rewrite of it writes the whole store's worth of
  paths. Coalescing the rewrite is what keeps that from being quadratic in the
  size of the workspace, at the price of a crash leaving records the index does
  not name — which the sweep already collects on the next launch, so the cost is
  the last few files analysed again.
- Two instances of the application open on one folder quietly undo each other's
  store. The second to start prunes against a snapshot of the first's index and
  sweeps every record the first has written since; each then replaces the
  other's index in turn. Nothing corrupts — the writes are atomic and a missing
  record is only ever a miss — but the store converges on useless for both, and
  whichever instance closed first finds none of its second-launch promise kept.
  No directory lock was taken, deliberately: a lock brings failure modes worse
  than the problem it solves, because a stale one left by a crash needs a
  timeout or a liveness check, and getting either wrong locks a user out of
  their own cache with no way to tell why. The consequence here is a degraded
  cache on an unusual workflow, never corruption and never lost work, which is
  not worth buying at that price.

### Neutral

- `Has` answers from the index copy that was checked when the root was pointed
  at, so a file changed since then reads as a hit and is refused later by
  `Read`. That costs one skipped file in one session and never shows a stale
  colour.

## Alternatives Considered

### Persist inside `AnalysisCache`

The smallest diff, and the one that would have given the cache a second job:
budget eviction and durability pull in opposite directions, since the entry a
byte budget evicts first is exactly the one worth keeping on disk. Rejected for
that, and because it would have put a filesystem inside the one class in this
area that is tested without one.

### One file for the whole store

No index, no hashing, no sweep — a single file read and written whole. Rejected
because every write rewrites every entry: the pass finishes a file every few
seconds, and the store of a large workspace is megabytes, so the worker would
spend the session rewriting megabytes to append kilobytes. A truncation would
also cost the whole store rather than one record. The index pays a smaller
version of the same cost and is named under Consequences for it; the difference
is one of scale — a path and a stamp per entry rather than an overlay — and one
of choice, since an index rewrite can be coalesced and a record cannot.

### A user-level store outside the workspace

Keeps the workspace clean and survives a deleted `.graphite`. Rejected because
the store is then keyed by absolute path in a directory nothing prunes when a
project is deleted or moved, and because a stale store is hardest to reason
about precisely when nobody can find it. Inside the workspace, the existing
`.gitignore` entry already covers it and deleting the folder is a complete
reset.

### Version the records and migrate them

Rejected without much hesitation: there is nothing to migrate to. Every byte in
the store can be recomputed from the workspace, so a migration would be code
written to preserve data that is cheaper to discard than to convert.

## Related

- [0511](0511-analyse-the-workspace-ahead-of-the-reader-several-files-at-a-time.md) —
  the background pass this is the durable tier of.
- [0512](0512-name-one-owner-for-every-document-opened-with-a-language-server.md) —
  the ownership rule under the pass that fills this store.
- `Graphite/src/Lang/Analysis/AnalysisStore.h` and `.cpp` — the index, the
  sweep, the synchronous read and the worker's writes.
- `Graphite/src/Editor/AnalysisCache.h` — the memory tier above it, asked first
  because a memory hit is both fresher and free.
