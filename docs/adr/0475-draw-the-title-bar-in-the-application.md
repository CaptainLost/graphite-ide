# 0475. Draw the title bar in the application

Status: accepted
Date: 2026-08-11

## Context

Windows drew the caption: an icon, a title, and the three system buttons, in a
row the application did not control. That row cost real height above the menu
bar, and nothing in it could be themed — it stayed whatever colour the OS gave
it regardless of what Graphite drew beneath. The row directly below, holding
the menus, had empty space to its right that the system bar's presence made
awkward to use for anything else.

## Decision

The window keeps `WS_OVERLAPPEDWINDOW` and its native, OS-drawn frame.
Graphite only intercepts two messages: `WM_NCCALCSIZE`, to claim the caption's
pixels for client area, and `WM_NCHITTEST`, to answer which part of that
reclaimed area the cursor is over. Everything else about the frame — the drop
shadow, the snap animations, Aero Snap, edge resizing — is left to Windows,
because the frame itself is never replaced, only the row painted inside it.

`core::Classify` (declared in `Core/Platform/Caption.h`) is the pure function
behind the hit test: given a point and a `CaptionMetrics` — caption height,
resize border width, and the horizontal span of the row the application left
free — it returns `Client`, `Caption`, or one of eight resize edges. That span
is geometry, deliberately not "is the cursor over a widget": answering
`HTCAPTION` hands the pointer to the non-client area, at which point the
application is no longer told where the cursor is, so a cursor-derived answer
would invalidate itself the moment it was given. Graphite reports those
metrics once per frame through `Window::SetCaptionMetrics`, taking the span
from where the menus ended and where `core::LayOutCaption` (in
`Core/Gui/CaptionLayout.h`) placed the three window buttons — plus one flag
that withholds the drag while a menu is open, so the click that closes it
reaches the application. `WindowFrame::Hide(Window&)` is the seam that does the
subclassing; it lives in Core behind `Core::Platform`, in line with
[0002](0002-keep-core-free-of-ide-knowledge.md), and returns `Unsupported` on
any platform, or any failure, where hiding the native bar cannot be done
safely — leaving the system bar in place rather than a window with no caption
at all.

## Rationale

Reimplementing the frame — resize regions, snap, the shadow — is a lot of
Win32 to get exactly as right as the OS already has it. Intercepting two
messages and answering a hit test gets the visual and interaction result of a
custom title bar while the frame that makes a window feel native keeps working
because it was never touched.

## Consequences

### Positive

- Aero Snap, the drop shadow, the snap animations and edge resizing are
  inherited, not reimplemented, so there is no dragging or resizing logic to
  get wrong.
- The row above the menus now belongs to the application: it shows the
  dimmed `Graphite <version>` label and can be themed like the rest of the
  window.
- The platform seam is one function, `WindowFrame::Hide`, so a port to another
  platform writes one branch and nothing else in Core or Graphite changes.

### Negative

- The hit test answers from caption metrics that are one frame old, because
  `WM_NCHITTEST` runs on the window thread ahead of the next ImGui frame.
  This is invisible at frame rate but is a real, permanent lag rather than an
  edge case that was missed.
- Windows 11 Snap Layouts need the window to answer `HTMAXBUTTON` from the hit
  test and are not implemented here, so hovering the maximize button gives no
  flyout the way a native caption button would.
- An auto-hidden taskbar cannot be summoned by moving the cursor to the screen
  edge while the window is maximized, because that gesture is wired to the
  native caption's hit-test region, which a maximized custom-drawn window no
  longer presents in the same way.

### Neutral

- Everything the drawn caption costs is conditional on
  `Window::HasCustomTitleBar()`: where `Hide` refused, the menu bar keeps its
  ordinary height and its ordinary padding, so the degrade path is the
  application exactly as it was before this record.
- `Core::Gui::DockSpaceHost` gained `DockLayout::menuBarHeight` so the extra
  row's height is pushed only around the host window's `Begin`; pushing it any
  wider leaked into the dock nodes' tab bars and offset every docked panel's
  content.

## Alternatives Considered

### `GLFW_DECORATED = false`

Portable — no Win32 subclassing, no `WM_NCHITTEST` at all. Rejected because it
throws away the native frame outright: Aero Snap, the drop shadow and edge
resizing all come from `WS_OVERLAPPEDWINDOW`, and losing the frame means
hand-rolling window dragging and resizing in application code, which is a
larger and more failure-prone surface than the two messages this decision
handles instead.

### Keep the system title bar

Free — no seam, no hit test, no `Unsupported` fallback to reason about.
Rejected because it is the status quo this record replaces: it wastes a row
above the menus and cannot be themed, and both were the actual complaints
driving the change.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — why `WindowFrame` and
  `Classify` live in `Core/Platform` behind `Result<T>` rather than in
  Graphite.
