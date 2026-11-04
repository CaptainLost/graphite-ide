# 0671. Show a branch's history by first parents by default

Status: proposed
Date: 2026-11-04

## Context

The history of a branch or of HEAD lists every commit with its full graph. In a repository where pull requests are merged, that is the commits of every merged branch interleaved with the line's own, and a branch that holds a few hundred merges shows thousands of rows and as many lanes. Most readers ask "what landed on this line, and when", which the line's first-parent chain answers with one row per merge, squash or direct commit.

## Decision

- A branch's or HEAD's history asks the log with `LogRequest::FirstParentOnly` and lays the graph out with `LayoutParents::FirstOnly` by default. The header carries a switch, `Merges` and `All commits`; `All commits` asks the full log and the full graph. Switching asks the log again from the start.
- The switch applies to a branch's and HEAD's history only. A stash is one commit, and a file's history follows the file through merges, so both keep the plain log and the switch is drawn dim for them.
- Showing a commit from outside the window (a blame or search jump) switches to `All commits`, because the commit may sit on a merged branch that first parents leave out. The switch stays on `All commits` until the reader switches it back or the folder changes.
- The choice lives in the window and is not saved; a new folder starts on `Merges` again.
- The vs button reserves room for the behind count that is measured later (as wide as five digits and a comma, or the value if wider), so the header row does not move when it lands.

## Rationale

First parents make a merge-heavy history readable and cheap to lay out, and the switch is one click away when the full graph is wanted. Keeping the default in the log request, not only in the drawing, spares the walk of commits the rows would never show.

## Consequences

### Positive

- One row per landed change on a line, and a graph with a single lane for a line that merges many branches.
- The log walks fewer commits for the same page.

### Negative

- A text filter in `Merges` finds only commits on the first-parent chain; a commit that arrived inside a merge is found after switching to `All commits`.
- A reader who wants the full graph has one more click to make on every start.

### Neutral

- The graph is laid out again when the switch changes, since the layout is keyed by the page and the mode.

## Alternatives Considered

- **All commits by default, first parents on request.** Matches what git prints, but a merged branch's commits drown the line in the repositories this window is built for.
- **Hide merged commits in the drawing only.** Asks and walks the full log for rows that are never shown, and a page of 500 would hold far fewer than 500 visible rows.

## Related

- [0663](0663-compare-each-branch-with-the-line-it-forked-from.md) — the line the header says a branch is compared with.
