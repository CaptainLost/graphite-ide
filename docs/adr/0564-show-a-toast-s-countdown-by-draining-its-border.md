# 0564. Show a toast's countdown by draining its border

Status: proposed
Date: 2026-10-06

## Context

Success and Warning toasts leave six seconds after they appear, the countdown
paused while the pointer is on them; an Error toast stays until it is closed
([0557](0557-report-a-failed-action-in-a-toast-rather-than-in-the-panel-that-issued-it.md)).
Nothing on screen said which kind a toast was in that respect, or how long one
that leaves had left: a toast vanished while it was still being read, and an
error looked no different from a toast that would go by itself.

The card already has a border in its kind's colour. The frame loop sleeps when
nothing changes ([0530](0530-draw-a-frame-only-when-there-is-reason-to.md)), so
anything that moves on screen for seconds costs frames for all of them.

## Decision

Show the time a toast has left by draining its coloured border.

- The border drains clockwise from the middle of the top edge, at an even speed
  along its length, and is gone when the countdown ends.
- The drained part stays in the kind's colour at a low alpha, so the card keeps
  an edge.
- The border stops while the countdown is paused and fills again when a twin
  restarts it. A toast closed by hand fades with the border it had.
- An Error toast keeps its whole border: a border that never drains says the
  toast stays until it is closed.
- While any countdown runs, the toasts ask for a frame every sixtieth of a second,
  not every frame; a paused or finished countdown asks for none.

The geometry lives in `Core/ImGui/ToastBorder`, apart from drawing, and the share
of lifetime left in `ToastStack::ShareOfLifetimeLeft`, so both are tested
without ImGui.

## Rationale

The border is already there, so the countdown adds no element to a card that
holds a title, a message and a close button, and a toast that will not leave
by itself is told apart by the same means. Sixty frames a second keep a
draining line smooth at a fraction of the monitor's rate.

## Consequences

### Positive

- Whether a toast will leave by itself, and when, can be read at a glance.
- Pausing on hover becomes visible: the border stops.

### Negative

- A Success or Warning toast keeps the loop awake for its whole lifetime, at
  sixty frames a second, where it used to sleep until the expiry.
- A thin line is quieter than a bar; on a busy screen the countdown is easier to
  miss.

### Neutral

- An Error toast looks exactly as it did.

## Alternatives Considered

- **A progress bar along the bottom of the card.** The most familiar, and easier
  to see from afar, but one more element on the card, and no different in cost.
- **A ring around the close button or the kind's icon.** Compact, but small
  enough to miss, and draws the eye to the button rather than the message.
- **Seconds written beside the close button.** Exact and nearly free in frames,
  but adds text to a card that is already text.
- **A bar in segments that go out once a second.** Six frames instead of
  hundreds, but it moves in jumps.

## Related

- [0557](0557-report-a-failed-action-in-a-toast-rather-than-in-the-panel-that-issued-it.md)
  — the toasts, their lifetimes and the hover pause.
- [0530](0530-draw-a-frame-only-when-there-is-reason-to.md) — why the countdown's
  frames are rationed.
