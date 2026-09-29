# 0529. Run system dialogs on a thread of their own

Status: accepted
Date: 2026-09-29

## Context

`Core::FileDialog` shows the Windows shell's folder and file pickers through
`IFileOpenDialog::Show`, which blocks its thread until the dialog is answered.
It used to run on the main thread, the one that pumps the window's messages
and draws every frame.

Closing a shell dialog does not always end when it leaves the screen. On the
way out, `comdlg32` shuts down the shell's change-notification thread and
waits for it (`SHChangeNotifyRegisterThread`). That wait often lasts five to
six seconds, and meanwhile the thread retrieves no input. After five seconds
without input Windows marks every window on that thread Not Responding and
covers it with a ghost that takes the input, until the wait ends. The stall
belongs to the shell, and no option on the dialog removes it.

Three places open a dialog: Open Folder, Open Solution and Add Existing
Project in the solution tree.

## Decision

- Open every system dialog through `Core::AsyncDialog`, which calls the
  picker on a new thread and hands the result to a callback on the thread
  that polls it. Never call a `FileDialog` picker from the main thread.
- Keep one `AsyncDialog` for the application, owned by `Workspace` and
  polled at the top of `Workspace::Update`. Only one dialog is open at a time,
  and a second `Start` is refused.
- Disable every control that opens a dialog while one is open: the commands
  through `Enabled`, and the buttons and menu items through `IsDialogOpen`.
- Give the dialog no owner window.

## Rationale

The stall cannot be removed, only moved. On a thread of its own it stops at
that thread, so the window keeps drawing and answering input while the shell
finishes. The thread is created per dialog and detached, so neither the
dialog nor the stall can hold up anything else.

## Consequences

### Positive

- Cancelling or answering a dialog never makes the IDE stop responding,
  however long the shell takes to clean up.
- The IDE keeps drawing while a dialog is open, so it does not look frozen
  behind it.

### Negative

- The answer arrives on a later frame, through a callback. A caller cannot
  use the picked path on the line after it opens the dialog, and a callback
  has to hold on to whatever it acts on. Add Existing Project, for example,
  carries its pending action into the callback.
- Without an owner the dialog is not modal. The IDE can still be clicked
  while the dialog is open, so every control that opens a dialog has to be
  disabled by hand, and a new one that is not stays clickable and does
  nothing.
- A dialog still open when the application exits is left running on its
  detached thread until the process ends.

### Neutral

- Each picker initialises COM on its own thread, as before, now on the
  dialog's thread rather than the main one.

## Alternatives Considered

- **Keep the dialog on the main thread and give it the main window as
  owner.** This makes the dialog modal, but the close still waits on the
  shell's notification thread on the main thread, so the window is still
  marked Not Responding. It changes nothing about the stall.
- **Run the dialog on its own thread with the main window as owner.**
  Windows attaches the input queues of an owned window's thread and its
  owner's thread, so the dialog thread's stall would again leave the main
  window without input.
- **Keep the main thread and stop tearing down COM after each dialog.** Tried
  while diagnosing. The stall happened just the same, because the wait is
  inside the dialog's own close, not in COM's teardown.

## Related

- `Core/src/Core/Platform/AsyncDialog.h`, `Core/src/Core/Platform/FileDialog.h`
