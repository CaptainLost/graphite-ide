# 0555. Share one text selection model across views that show text

Status: proposed
Date: 2026-10-04

## Context

Three views show text a reader wants to copy: the console, the terminal and the
diff. Each had, or lacked, a selection of its own. The console anchored a
selection to a log entry's sequence number (0503). The terminal swept a
rectangle of cells, which is a block selection, while every other text surface
selects a stream. The diff had no selection at all, so a reader who wanted three
lines of a hunk copied the file elsewhere.

Each view numbers its lines differently and each of those numberings moves: the
console drops its oldest entries and renumbers under a filter; the terminal
pushes rows into its scrollback; the diff changes with its mode and scope. A
selection that remembers a row index slides away from its text in all three. And
each view had its own idea of what a double-click, a triple-click, Shift+click,
Ctrl+C and Ctrl+A do, so the gestures a reader learned in one did not carry to
the next.

## Decision

One selection model in Core, one gesture handler in Core, and each view supplies
only what it alone knows.

`Core::TextSelection` holds two ends, the pressed one and the dragged one, each
a `TextPoint` of a line number the owner chooses and a column in the owner's
unit. It holds no text: it says which columns of a line are selected, and the
owner, who has the text, reads them. `Core::WordBounds` finds the word around a
byte, in two kinds: `Code` (letters, digits, `_`, non-ASCII) and `Path` (those
plus `/ \ . - : ~`, which keep a file path and its line suffix whole).

`Core::SelectionGesture` turns input into selection changes: a drag extends,
double-click selects a word, triple-click a line, Shift+click extends the
existing selection, Ctrl+C copies, Ctrl+A selects everything, and a shared
Copy/Select All context menu is available. The owner provides a
`SelectionSource` of callbacks in its own units. `PointAt` gives the boundary
between characters nearest the pointer and serves drags and Shift+click.
`CharacterAt` gives the character under the pointer and serves the double-click
word and triple-click line, so a double-click on the right half of a word's last
character still takes that word. `Claims` lets the owner keep a press for itself,
such as a scrollbar drawn without an item. Options per view switch off what the
view routes itself: copy key, select-all key, menu, and auto-scroll directions.

Each view numbers its lines so that a selection follows its text:

- Console: the line is the log entry's sequence number (0503).
- Terminal: the selection is linear; the rectangle is gone. The line is the
  number of rows ever scrolled into the scrollback plus the row, so a selection
  follows its text as output scrolls. It is cleared when its text leaves the
  history, and when the screen switches to or from the alternate buffer, whose
  rows are a different text. Ctrl+C keeps the semantics of 0546. Select All is
  in the terminal's own menu; Ctrl+A stays with the shell.
- Diff: in side-by-side, the selection is confined to the side the press began
  on, and rows that side does not have are skipped. In unified, removed and added
  lines are copied as shown, without the `+` and `-` prefixes. Headers and gaps
  are never selected. The selection is cleared when the mode or scope changes, or
  when the content hash changes: the hunks, plus the whole file's texts in
  whole-file scope.

The highlight uses the editor scheme's Selection colour in the terminal and the
diff, and the UI default in the console. The editor's own selection is unchanged.

## Rationale

A selection that stores owner-numbered lines and no text survives the churn each
view has, and the owner already holds the text. Keeping the gestures in one place
makes a double-click mean the same everywhere and lets the hit-testing rules,
which are subtle, be written and tested once. The views stay different where
their content is different, which is only in how they number lines and what they
exclude.

## Consequences

### Positive

- A reader selects and copies the same way in the console, the terminal and the
  diff.
- A selection follows its text instead of its row.
- A new view that shows text needs a few callbacks, not a selection of its own.

### Negative

- The terminal loses rectangular selection, which some readers used to copy a
  column of output.
- Soft-wrapped terminal rows are not joined: a long line that wrapped copies as
  several lines.
- A selection in the terminal or the diff is cleared when its text changes under
  it, rather than kept in some adjusted form.

### Neutral

- The editor's selection is unchanged and stays outside this model.

## Alternatives Considered

- **A selection per view, as before.** Rejected: the same gestures and the same
  hit-testing mistakes would be rewritten three times and drift apart.
- **A selection that stores the selected text.** Rejected: it would go stale as
  the buffer moves, and the console already showed that a position the owner can
  resolve is enough (0503).
- **Keeping rectangular selection in the terminal alongside the linear one.**
  Rejected: it needs a modifier and a second mode in a model whose point is one
  behaviour, and no other surface offers it.

## Related

- [0495](0495-draw-a-side-by-side-diff-in-one-scrolling-region.md)
- [0503](0503-anchor-a-console-selection-to-a-log-entry-s-sequence-number.md)
- [0544](0544-draw-each-terminal-as-a-window-of-its-own-docked-beside-the-last-one-used.md)
- [0546](0546-route-editor-scoped-key-commands-from-the-focused-editor.md)
- Spec: `docs/superpowers/specs/2026-10-04-shared-text-selection-design.md`
