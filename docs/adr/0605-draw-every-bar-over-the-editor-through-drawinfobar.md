# 0605. Draw every bar over the editor through DrawInfoBar

Status: proposed
Date: 2026-10-16

## Context

Two bars sit over the editor: restricted mode, and missing language support. They looked the same, so a warning could not be told from a piece of information. Every button had the same weight. The restricted mode bar could not be closed, and a long parent path on its "Trust All in" button ran out of the window.

## Decision

Draw every bar over the editor with `Graphite::DrawInfoBar` (`Graphite/src/UI/InfoBars/InfoBar.h`).

- **Weight:** a bar is either information or a warning. Its glyph is drawn in the weight's colour: the accent for information, the scheme's warning colour for a warning. A warning also tints the bar's background toward that colour.
- **Message:** cut short with an ellipsis where it does not fit, with the whole message in a tooltip.
- **Buttons:** placed at the bar's end, before its close button, and fitted there before the message takes what is left.
  - One button is the main action and is filled with the accent, as a dialog's main button is.
  - The others have no fill at rest; on a warning they are hovered in the weight's colour, as the surface's own hover does not show on the tint.
  - A secondary label is cut short to the room left, and to a set width at most, with the whole label in a tooltip; a secondary with no room left is left out.
  - The labels are drawn by the bar, so a path holding "##", as a folder named C# does, shows whole.
- **Close button:** any bar that can be put aside has one.
  - The language bar's "Not now" is its close button.
  - The restricted mode bar's close button hides the bar for that folder until Graphite restarts.
  - While the folder stays untrusted, the language bar stays hidden even after the restricted mode bar is hidden.

## Rationale

**Weight shows.** A warning looks like a warning, and the one thing the bar asks for stands out from the rest.

**Nothing spills.** Fitting the buttons first and cutting the message and long labels keeps the bar to its one line, and the close button clear, however narrow the window.

## Consequences

### Positive

- The two bars look alike where they should, and differ where their weight differs.
- The restricted mode bar can be put aside without trusting the folder.

### Negative

- A hidden restricted mode bar comes back at the next start; there is no way to hide it for good short of trusting the folder.

### Neutral

- The status bar still shows the restricted mode while the bar is hidden.

## Alternatives Considered

- **Only a main button and cutting, without the weight's colour.** A smaller change, but a warning still would not look different from information.
- **Hide the restricted mode bar until the folder is reopened.** Closer to how a session runs, but an accidental close would then last longer than the user meant it to.
