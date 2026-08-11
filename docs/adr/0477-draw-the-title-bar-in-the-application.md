# 0477. Draw the title bar in the application

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

The one part of the frame that reclaiming the caption also takes away is the
border, and that is asked back from DWM rather than drawn — but it takes two
things, not one, and the first is the one that is easy to miss. DWM draws the
border *into the frame*: the band between the window rectangle and the client
rectangle, which is exactly what `WM_NCCALCSIZE` decides the width of. Handing
the proposed rectangle back untouched leaves that band zero pixels wide on
every edge, and a border zero pixels wide is indistinguishable from no border
however it is coloured. So a restored window's client rectangle stops one
`SM_CXBORDER`/`SM_CYBORDER` short of the window rectangle on all four edges —
one physical pixel each at 96 DPI on build 26200, asked per monitor rather
than hard-coded so a Windows that scaled its border would still get one — and
that is the room the border lives in. A maximized window keeps its existing frame-sized
inset instead and gets no border: every edge it has is off the screen already.

The colour is then `DWMWA_BORDER_COLOR`, set to the colour `ImGuiCol_Border`
already outlines every panel with, so the window's edge matches the edges
inside it. The two literals — this one and the theme's — have to move together;
the frame cannot read the style, because it runs before ImGui exists. The
attribute exists from build 22000; older systems answer with a failure
`HRESULT`, which is warned about and otherwise ignored, so `Hide` still
succeeds and `HasCustomTitleBar()` stays true — the frame is still there, it
just keeps whatever colour the system paints an ordinary window's with.

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
reaches the application.

The three window buttons ask `Classify` the same question about the pointer, so
the highlight and the hit test cannot disagree. Along the top and right edges
the resize border runs straight across them: there the press is non-client
input the application never receives, and a themed `ImGui::Button` would light
up under a cursor that will never click it. Each button is therefore an
`InvisibleButton` with its background and glyph on the draw list, submitted
inside `BeginDisabled` whenever `Classify` calls the pointer anything but
`Client` — which is what keeps ImGui from reporting it hovered, from taking
the press, and from holding an active id in that strip.

`WindowFrame::Hide(Window&)` is the seam that does the
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
- The window border is drawn by DWM, outside the client area, so it follows the
  rounded corners by itself and costs no per-frame work.

### Negative

- The hit test answers from caption metrics that are one frame old, because
  `WM_NCHITTEST` runs on the window thread ahead of the next ImGui frame.
  This is invisible at frame rate but is a real, permanent lag rather than an
  edge case that was missed.
- Windows 11 Snap Layouts need the window to answer `HTMAXBUTTON` from the hit
  test and are not implemented here, so hovering the maximize button gives no
  flyout the way a native caption button would.
- A restored window gives up one physical pixel of client area on each of its
  four edges so the border has somewhere to be. Every client coordinate stays
  measured from the client origin — the caption strip, the button rectangles
  and the hit test all move together with it — so nothing needed compensating,
  but a 1600x900 window now has a 1598x898 client area.
- Before build 22000 the border keeps the system's colour rather than the
  theme's: `DWMWA_BORDER_COLOR` is refused there, and the alternative that
  would have themed it — `DwmExtendFrameIntoClientArea` with a one-pixel top
  margin — cannot show through a client area the renderer clears opaque every
  frame. The outline itself is there on those systems, because it comes from
  the frame `WM_NCCALCSIZE` leaves and not from the attribute.
- The hit test is now asked about points outside the client rectangle, because
  the border strip belongs to the window and `WM_NCHITTEST` covers it.
  `Classify` answers them as the edge they lie beyond, so the border resizes
  like the pixels just inside it.
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

### Colour the border and leave the frame at zero width

What this record said first, and it shipped without an outline. Setting
`DWMWA_BORDER_COLOR` on a window whose `WM_NCCALCSIZE` returns the proposed
rectangle unchanged succeeds — `S_OK`, measured — and shows nothing, because
the frame it colours is zero pixels wide on all four edges. Measured on build
26200: before the subclass the client sat 8 px inside the window rectangle on
the left, right and bottom and 31 px below its top, with the visible edge
7 px inside the window rectangle, so exactly one pixel of that 8 was the
border. After the subclass all four gaps were 0 and the extended frame bounds
had collapsed onto the window rectangle. Rejected because it is a mechanism
the evidence shows cannot work, not a policy that could be tuned.

### Give the whole default frame back on the three non-caption edges

Inset the client rectangle by the full `SM_CXSIZEFRAME + SM_CXPADDEDBORDER` on
the left, right and bottom, which is what an ordinary window does, and take
only the caption from the top. Correct, and it restores the invisible resize
margin as well. Rejected because 7 of those 8 pixels per edge are invisible
grab area that this window does not need — it answers the resize edges itself
from `Classify`, over a strip it controls — and spending 8 px of client area
per edge to gain the same one-pixel line is a worse trade than spending one.

### Draw the border in the application

One rectangle around the dockspace host, one pixel wide, redrawn every frame.
It would work on every Windows version, including the ones
`DWMWA_BORDER_COLOR` leaves without a border. Rejected because it spends an
interior pixel and a per-frame draw on something the system draws outside the
window for nothing, and because a rectangle in the client area cannot follow
the rounded corners Windows 11 gives the window — it would cut across them.
The same objection sinks `DwmExtendFrameIntoClientArea` with a one-pixel top
margin, which was tried first: the extended row stays the application's, and
`Core::Gui::ImGuiRenderer` clears the whole client area opaque every frame, so
nothing DWM draws underneath it can ever be seen.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — why `WindowFrame` and
  `Classify` live in `Core/Platform` behind `Result<T>` rather than in
  Graphite.
