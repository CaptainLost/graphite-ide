# 0557. Report a failed action in a toast rather than in the panel that issued it

Status: proposed
Date: 2026-10-04

## Context

A failed git write was drawn as a wrapped line with a Dismiss button in the panel
that issued it
([0500](0500-serialise-git-writes-in-one-queue.md),
[0541](0541-put-branch-and-stash-actions-in-the-git-panel-s-right-click-menus.md)).
The line pushed the Changes list and the commit box around when it appeared,
stayed until dismissed, and was invisible while the panel was closed. The Git
panel had a second such line for its own action errors, so two panels could show
the same failure, or one could show it while the other did not.

## Decision

A click's failure is reported through `Core::Toasts`, owned once by the
application layer and handed by reference to whatever reports
([0022](0022-i-3-avoid-singletons.md)).

- An Error toast stays until it is closed. Success and Warning toasts leave after
  six seconds, the countdown paused while the pointer is on them.
- Each toast carries a title naming the failed action - "Commit failed" - drawn
  in JetBrains Mono Bold above the message, which is drawn in the regular face.
- A message identical in title and text to one still shown is counted on that
  toast, not repeated.
- `GitModel` queues write failures and hands each out once through
  `TakeWriteError()`, each with the kind of write that failed; the application
  layer drains them every frame and turns each into a toast, titled from that
  kind. The model knows nothing about toasts and holds no UI text.
- A lasting state stays where it is: a refresh error in the Git panel, and the
  failure of `git init` in its own prompt (`GetInitError()`,
  `DismissInitError()`).

Any feature raises a toast by publishing `ToastEvent` on the application's
`Core::EventBus`, the way `StatusMessageEvent` reaches the status bar; only the
application layer holds `Core::Toasts`. Each event carries a scope. A `Folder`
toast is about the open folder or its repository - every git failure is one -
and is cleared when that folder closes or another replaces it; an `Application`
toast survives that. Core keeps the scope as an opaque group tag on the toast,
which `Clear(group)` removes alone and which takes part in merging twins. Errors
and warnings go to toasts; routine confirmations - saved, reloaded, committed -
stay in the status bar's transient slot. A menu's Copy is confirmed by a Success
toast titled "Copied"; plain Ctrl+C and Ctrl+X are not confirmed at all.

## Rationale

A failure is reported whether or not a Git panel is open, and exactly once when
both are open, because the application layer drains the queue rather than each
panel reading a shared field. The model stays free of UI. A later success no
longer erases a failure nobody saw, because the failure is queued rather than
held in a single slot. The panels' layout stops moving when an error appears.

## Consequences

### Positive

- A failure is never lost to a closed panel or overwritten by the next write.
- The Changes and Git panels lose their Dismiss bars and the layout shifts that
  came with them.
- Any other feature can report a failed click the same way, through the same
  stack.

### Negative

- The message appears in the window's corner, away from the panel the click was
  in.

### Neutral

- Refresh errors and the `git init` failure are states, not answers to a click,
  and are shown where they were.

## Alternatives Considered

- **Keep the line in each panel and fix its layout.** Reserving room for it stops
  the jumping, but the failure is still invisible with the panel closed and still
  shown twice with both open.
- **Have the model push toasts itself.** Reports once and everywhere, but ties
  the model to the UI and makes it untestable without one.
- **Show a modal dialog for each failure.** Cannot be missed, but interrupts work
  for something the user can usually ignore and retry.

## Related

- [0500](0500-serialise-git-writes-in-one-queue.md) — where a write failure was
  kept.
- [0541](0541-put-branch-and-stash-actions-in-the-git-panel-s-right-click-menus.md)
  — the Git panel's write error.
- [0022](0022-i-3-avoid-singletons.md) — the stack is owned once and passed by
  reference.
