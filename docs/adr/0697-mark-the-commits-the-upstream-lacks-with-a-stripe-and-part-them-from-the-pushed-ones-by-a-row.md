# 0697. Mark the commits the upstream lacks with a stripe and part them from the pushed ones by a row

Status: accepted
Date: 2026-11-10

## Context

The Git window's history told a commit the upstream lacks from a pushed one only by the
colour of its hash, at the row's right end, and showed where the pushed commits begin only
by the upstream's pill on the first of them. With 82 commits to push the pill sat 82 rows
down: at the top of the list nothing said which commits were only on this computer.

`Core::LogPage` already carries the set (`Unpushed`) and whether it can be trusted
(`UpstreamKnown`), measured against the walked branch's upstream so it agrees with the
branch's ahead count. A merge not yet pushed can bring commits that are pushed (the
upstream merged back in), so a merge's mark says nothing of the commits it brought. The
user chose between four row marks and three edges on a mockup of the list.

## Decision

- Tint every row that stands for a commit the upstream lacks — its commit row, the rows of
  its files, and a brought commit's row by that commit's own membership — with 8 percent of
  the added-line green, and draw a 3 px stripe of that green at the row's left end, in the
  room the row highlight leaves. The hash stays green as well.
- Put one row, `PushedEdge`, above the first commit the upstream has, when every commit
  above it is one the upstream lacks and none below it is (`FirstPushedCommit`). It says
  "↑ N to push" in green, a rule, then "on <upstream> ↓" at the message's end; the name
  goes first, then the rule, when the cell is narrow. N is the size of `Unpushed`, the
  header's count. The upstream is that of the walked branch (`UpstreamOfWalk`); while it is
  not known the row ends "pushed ↓".
- Draw no edge without a known upstream, with nothing to push, when the page does not reach
  a pushed commit, and when the two kinds are mixed in the list.

## Rationale

A stripe and a tint are seen anywhere in the list, without reading to the row's end, and a
run of them reads as one block. The edge row answers "where does origin start" without
hunting for a pill among others. Both came out of the mockup as the clearest pair.

## Consequences

### Positive

- Which commits a push would send is visible at a glance, at the top of a long list too.
- The pill of the upstream is no longer the only landmark of the edge.

### Negative

- The tint sits under the hover and selection highlights, which are a little less distinct
  over it.
- The edge is one row more, and the list's rows no longer all stand for commits or files.
- When the two kinds are mixed, as an old commit an unpushed merge brought listed by its date
  in All commits, there is no edge; only the stripes tell them apart.

### Neutral

- A file's history and a filtered page keep the marks: the set is the branch's, not the page's.

## Alternatives Considered

- Hollow dots in the graph and a dashed lane through the commits to push: no room taken,
  but a small dot is easy to miss.
- An arrow before the author on every such row: clear, but 82 identical icons are noise and
  it takes a column.
- A band pinned to the top of the list while the edge is out of view: answers at once, but
  repeats the header's "to push" count.

## Related

- [0593](0593-load-more-history-only-on-request-from-a-status-line-under-the-list.md)
