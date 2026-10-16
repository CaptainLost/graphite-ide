# 0607. Draw the middle of every notification through one piece of Core code

Status: proposed
Date: 2026-10-16

## Context

A notification is shown in two places: as a toast in the corner of the window, and as a row in the notification history. Both show the same things, an icon for its kind, a bold title with a dim repeat count after it, and a message whose strong runs are bold. Each place drew them in its own way.

- **The icon:** the toast centred it on the first line of text. The history worked out the same place on its own, from the row's inset and the icon's diameter, so the two agreed only while both sums did.
- **The title:** the toast wrapped it with `PushTextWrapPos` and placed the count with `SameLine`, at the column's edge when the title wrapped. The history clipped it at the time and the remove button, cutting a long title mid-letter.
- **The gap between title and message:** 4 px on a toast, none in the history.
- **The measurements:** the toast measured with `CalcTextSize`, which rounds each advance up, and the history with unrounded advances, so the same title took different widths.

The frame around the middle is a different matter. A toast is a card with a border that drains as it times out; a history row is flat, with the time and a remove button at its end.

## Decision

Draw the middle of every notification through `Core/src/Core/ImGui/ToastStyle.h`. Each place keeps its own frame.

- **Icon:** placed with `NotificationIconCentre(textLeft, firstLineTop)`. It sits in the column before the text, `NotificationIconGap()` short of it, centred on the first line of text. The middle is rounded up to a whole pixel.
- **Title:** measured with `NotificationTitleHeight` and drawn with `DrawNotificationTitle`, in bold, with the count dim after it, both measured with unrounded advances. `NotificationTitleFit::Wrap` wraps it over as many lines as it takes, the count after the widest of them. `NotificationTitleFit::Ellipsis` keeps it to one line, cut short with an ellipsis.
- **Message:** drawn with `DrawNotificationMessage`, `NotificationTitleGap()` below the title, its strong runs bold in the text colour, the rest in the message colour.
- **Where each fit is used:** a toast wraps its title. The history cuts its title short, so every row starts with a title one line high.

## Rationale

**One middle, two frames.** The user reads the same notification in both places, so its icon, title and message should look the same in both. The card and the flat row tell apart a notification that has just arrived from one in the record, and that difference is kept on purpose.

## Consequences

### Positive

- A notification's icon, title, count and message line up the same way on a toast and in the history.
- A long title in the history ends in an ellipsis instead of being cut mid-letter.
- The count after a wrapped title stands after the text, not at the column's far edge.

### Negative

- A history row is 4 px taller than it was, for the gap below the title.
- A long title in the history shows a few letters less than it did, the ellipsis taking their room.

### Neutral

- What frames the middle stays with each place: the card, its padding and its draining border on a toast; the time and the remove button on a history row.

## Related

- [0606](0606-draw-every-line-number-column-through-linenumbercolumn.md): the same push towards one look per kind of thing.
