# 0704. Fold code by indentation, each view on its own, by hiding rows in the row map

Status: proposed
Date: 2026-11-15

## Context

The editor could not fold a class, a function or a block. The gutter already had a one-cell column reserved for a
fold arrow, with nothing drawn in it. Every part of the editor reaches the screen through `RowMap`: the rows of each
line (more than one under word wrap), with `RowAt` and `FirstRowOf` over a Fenwick tree. Up and Down move by rows;
clicks, scrolling, lenses and the caret's place are all measured in rows.

Folding ranges could come from the language server (`textDocument/foldingRange`, not implemented), from document
symbols (only with a server, only after it answers), from the syntax tree (C# only), or from indentation.

## Decision

- Find what folds by indentation, in Core (`Core/Text/Folds`): a line folds the lines after it that are indented
  deeper, without the blank lines that end the block. A declaration whose brace stands alone on the next line at
  its own depth folds the brace and the block with it, and that brace line folds nothing of its own. The closing
  bracket stays in view.
- Keep folds per view (`OpenItem::Folds`), as each view keeps its own caret: two views of one file fold on their own.
  Folds are not saved.
- Hide a fold's lines by giving them no rows in the item's `RowMap` (`SetRowsOf(line, 0)`), and hide them again
  after anything rebuilds or edits the rows (`HideFoldedRows`, from `CatchUpWithDocument`, `AdoptLineCaches` and
  the editor's rewrap). Folds follow the text through the buffer's change log. An edit above a fold moves it, and an
  edit within its header's line keeps it. Any other edit that reaches the fold opens it.
- Keep the caret on lines in view. A step from a header into its fold goes to the line below the fold. A step back
  into it from there goes to the end of the header. A caret put inside a fold any other way, by a search or a jump,
  opens it.
- Show a chevron in the fold column, pointing right on a folded line always, and down on a line that could fold
  while the pointer is over the gutter. Put a marker with three dots after a folded line's text. A press on the
  chevron folds or opens, and a press on the marker opens.
- Commands Fold (Ctrl+Shift+[) and Unfold (Ctrl+Shift+]) act at the caret. Folding a folded
  block's line again folds the block around it. Fold All (Ctrl+Shift+Num−) and Unfold All (Ctrl+Shift+Num+) act on
  the whole file.

## Rationale

Indentation is there in every file at once, needs no server and no grammar, and for code laid out the usual way it
picks out classes, functions and blocks. Hiding rows in the map the whole editor already reads means motion, clicks,
scrolling and wrapping need no fold logic of their own. Up and Down skip a fold because it has no rows, and
everything measured in rows stays consistent.

## Consequences

### Positive

- Folding works in every language, including ones without a server, and in files that do not parse.
- Nothing outside Core's text model and the gutter had to learn about folds. Lenses only skip declarations with no
  row, and are placed again when the folds change (`OpenItem::FoldRevision`).

### Negative

- Code that does not indent its blocks does not fold: a C++ namespace body written flush left, or a one-line
  `if (x) { ... }`.
- Folds open on any edit that reaches them, including undo, so they are lost more often than in an editor that
  tracks ranges through edits. That includes a line added right above the header at its start (Enter at column 0,
  Duplicate Line on the header), because the change log gives an edit's first line but not where in the line it
  started.
- Change markers have no fold of their own: a change inside a fold shows nothing on its header in the gutter, and the
  scrollbar marks it on the line after the fold.
- A fold's lines are hidden by walking them after every edit, which costs time in proportion to the hidden lines.

### Neutral

- `#region` and comment blocks fold only when their lines are indented under a header.

## Alternatives Considered

- **Folding ranges from the language server.** Exact for C# and C++, but nothing without a running server. Ranges
  arrive after a round trip and go stale on the next keystroke. They can be added later as a better source for the
  same folds.
- **Ranges from the syntax tree.** Exact, but C# is the only language with a tree, and the tree is parsed in the
  background.
- **A hidden-line layer over `ScreenRows`, beside lenses and the peek.** Keeps `RowMap` about wrapping only. But Up
  and Down, clicks and the caret's place are worked out in document rows, so each would have had to skip folds
  itself.

## Related

- [0703](0703-help-typing-around-brackets-and-reindent-pasted-lines-from-the-text-alone.md)
