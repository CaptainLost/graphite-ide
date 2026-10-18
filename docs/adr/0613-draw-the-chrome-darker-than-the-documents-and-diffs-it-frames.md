# 0613. Draw the chrome darker than the documents and diffs it frames

Status: proposed
Date: 2026-10-18

## Context

The dark scheme drew its chrome (`#1B1D21`) in the same colour as the editor
background, so a document had no edge against the panels, title bar and status
bar around it. 0490 kept the chrome palette to one set of colours and foresaw
widening it; 0611 has a content window push the content colour before `Begin`,
as the tab text colour already does. The status bar also drew a border of its
own above the chrome beside it.

## Decision

- Draw the dark chrome with Background `#141518` and Border `#26292E`. Light
  keeps `#F3F3F3` under a white editor.
- The content colour is the scheme's editor background, carried to Core as
  `ChromePalette::Content`. That widens the palette, as 0490 foresaw, with no new
  scheme key.
- Documents, diffs, their selected tabs and the empty dock space are content and
  draw in the content colour. Every other view is chrome wherever it is docked:
  the Git view, review, sessions, settings, terminals and tool panels. A user
  scheme that sets `ui.background` keeps its value.
- Draw the title bar in the chrome. Draw the status bar with no border.

## Rationale

A chrome darker than the content makes the documents and diffs the lightest
part of the window, so the eye lands on them. The content colour already exists
as the editor background, so no scheme needs a new key and a user scheme keeps
working.

## Consequences

### Positive

- Documents and diffs stand out from the chrome that frames them.
- A scheme needs no new key; the content colour follows its editor background.

### Negative

- A scheme whose editor background is darker than its chrome gets content darker
  than the frame, with no way to say otherwise.
- Core's palette has one more field every caller must fill.

### Neutral

- Light chrome values are unchanged; its white editor already stands out.

## Alternatives Considered

- **A new `ui.chrome` key.** This lost because Background already paints every
  piece of chrome.
- **The content colour for the Git view, review, sessions and settings too.**
  This lost because their lists, cards and fields would need a second set of
  hover and selection colours, and light's white `Surface` would vanish on white.

## Related

- 0002, 0490, 0494, 0611
