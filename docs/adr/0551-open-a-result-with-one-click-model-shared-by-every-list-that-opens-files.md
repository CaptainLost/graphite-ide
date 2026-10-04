# 0551. Open a result with one click model shared by every list that opens files

Status: proposed
Date: 2026-10-04

## Context

Several lists lead to a file: the file tree, the solution tree, Problems, Find
in Files and References. Each decided on its own what a click did. Problems
opened a permanent tab on a single click, so walking down a list of fifty
problems left fifty tabs behind; Find in Files previewed on a single click and
opened on a double click but ignored Enter; the file trees previewed on a
single click and also ignored Enter. The same gesture meant different things a
few pixels apart.

## Decision

Every list whose rows lead to a file answers the mouse the same way:

- a single click selects the row and previews it - in the editor's preview tab,
  or in the panel's own preview pane where the panel has one;
- a double click opens the file permanently.

Enter opens the file permanently too, acting on the row keyboard navigation
has reached (the row ImGui's navigation activates), in every one of these lists.

Moving between rows with the keyboard is not yet the same everywhere:

- Find in Files and References select the row keyboard focus reaches, and
  their preview pane follows it;
- Problems moves its highlight to the row keyboard focus reaches, but opens
  nothing until a click or Enter;
- the file tree and the solution tree neither select nor preview on keyboard
  movement; only a click or Enter acts on a row.

The decision between preview and permanent open is made in one place,
`OpensPermanently` in `Graphite/src/UI/OpenGesture.h`, asked on the frame the
row's `Selectable` reports an activation. A new list that opens files asks it
too rather than reading the mouse itself.

## Rationale

Results are browsed far more often than they are kept: the reader looks at
several to find the one that matters. A preview that is replaced by the next
one costs nothing to look at, and a permanent tab is one deliberate gesture
away. One function makes the rule hard to break by accident, since a list that
reads the mouse on its own is visibly the odd one out.

## Consequences

### Positive

- Arrowing through results never piles up tabs.
- A gesture learnt in one panel works in every other.

### Negative

- Opening a result for good takes a double click or Enter where Problems used
  to take one click.
- Enter on a row is spent on opening; a list cannot give Enter a meaning of its
  own without breaking the rule.
- Keyboard movement is not yet uniform: arrowing through the search lists
  previews, through Problems only highlights, and through the trees does
  neither, so the same keys still mean different things from panel to panel.

### Neutral

- Space, which ImGui's navigation also treats as activation, selects and
  previews like a single click.

## Alternatives Considered

- **Single click opens permanently everywhere.** Lost because it is the
  behaviour that left a tab per inspected result.
- **Leave each panel its own rule.** Lost because the differences were not
  chosen for any panel's needs; they were accidents of the order panels were
  written in.

## Related

- [0489](0489-open-a-previewed-file-with-the-language-server.md) - what a
  preview tab holds.
