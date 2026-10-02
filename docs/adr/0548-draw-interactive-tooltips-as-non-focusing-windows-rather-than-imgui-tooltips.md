# 0548. Draw interactive tooltips as non-focusing windows rather than ImGui tooltips

Status: proposed
Date: 2026-10-02

## Context

The editor's hover shows a symbol's information, a preview of its definition
and any diagnostic under the mouse, in one box under the token. The user has to
be able to reach that box: move the mouse into it, scroll a preview longer than
the room the editor leaves it, drag its scrollbars, and select nothing else on
the way.

ImGui's own tooltip (`BeginTooltip`) cannot do any of that. A tooltip window is
never hovered, so the mouse over it is still over whatever lies beneath, and it
takes no wheel input, so a long preview cannot be scrolled. It also follows the
mouse rather than staying under the token.

An ordinary window brings problems of its own. A window that is never focused
falls behind every window focused after it, so the first click into the editor
would bury it for good. A click inside it focuses it, and the editor then loses
the keyboard to a box that goes away when the mouse leaves. With
`AlwaysAutoResize` its size is known only after it has been drawn, but its
position has to be set before.

Peek definition and any later hoverable tooltip face the same constraints.

## Decision

Draw an interactive tooltip as an ordinary ImGui window, not with
`BeginTooltip`, and give it the following rules.

- **Flags.** The window takes `NoFocusOnAppearing`, `NoNav`, `NoTitleBar`,
  `NoResize`, `NoCollapse`, `NoMove`, `NoDocking`, `NoSavedSettings`,
  `AlwaysAutoResize` and `HorizontalScrollbar`. It is
  dressed in the popup's colours, rounding and border, so it still reads as a
  tooltip.
- **One window per owner.** Name it after its owner's state (`##HoverTooltip%p`
  for the hover), so two editors side by side never append to each other's.
- **Raised every frame.** Call `BringWindowToDisplayFront` on every frame it
  shows, which moves no focus.
- **Placed from last frame's size.** Keep the window's rectangle from the frame
  before. Place it below the anchor if it fits, above if that fits instead, and
  otherwise on whichever side has more room. When the size changes, request one
  more frame.
- **Clamped to the editor.** Clamp it to the editor surface's visible
  rectangle, falling back to the work area of the editor's own viewport, and pin
  it to that viewport with `SetNextWindowViewport`. It may be as wide as that
  rectangle and as tall as the room on its side; what does not fit scrolls in
  the window itself, never in a child of it, so there is one pair of scrollbars.
- **Focus handed back.** Once no mouse button is down after a click inside the
  tooltip, return focus to the window that held it before the click. Fall back
  to the surface the tooltip belongs to if no window held it or that window has
  gone.
- **Nothing over a modal.** While a modal popup is open, neither raise nor draw
  the tooltip.
- **Separate router slots.** The hover's questions to the language server go
  through `LspAnswerRouter` slots of their own, one for the information and one
  for the preview's definition. A hover never cancels F12's question, and F12
  never cancels a hover. Cancelling a hover cancels only the asking document's
  questions.

## Rationale

A window is the only ImGui object that can be both hovered and scrolled, so a
tooltip the user can enter has to be one. The rules above turn back the
behaviour a window has and a tooltip should not have: taking focus, falling
behind, and following the window stack's order. They do this in the one place
that draws the tooltip, so nothing else in the editor has to know about it.

## Consequences

### Positive

- The mouse can enter the tooltip, the wheel scrolls its preview natively, and
  the scrollbar can be dragged.
- The keyboard stays with the editor the user was typing in, even after a click
  inside the tooltip.
- A later interactive tooltip, such as peek definition, can reuse the same rules
  rather than rediscover them.

### Negative

- Placement is always one frame stale. A tooltip whose content changes size is
  placed for its old size for one frame, which costs an extra frame each time.
- The tooltip relies on `imgui_internal.h` (`BringWindowToDisplayFront`,
  `FocusWindow`, `FindWindowByID`, `GetTopMostPopupModal`). An ImGui upgrade can
  break it in ways the public API would not.
- While the tooltip is focused, which lasts from the click until the button is
  released, keys go to the tooltip and not to the editor.

### Neutral

- Being raised every frame, the tooltip draws over other ordinary windows,
  including panels docked beside the editor, for as long as it is shown.

## Alternatives Considered

- **`BeginTooltip`.** It gets the look and the stacking for free. It loses
  because a tooltip window can be neither hovered nor scrolled, which rules out
  a preview longer than the box and any tooltip the user means to read at
  leisure.
- **A popup (`BeginPopup`).** A popup can be hovered and scrolled. It loses
  because opening it takes the keyboard from the editor, and it closes on any
  click outside it, including the click that should move the caret under it.
- **A focused window that gives focus back on leave.** It would avoid raising
  the window every frame. It loses because the editor would lose the keyboard
  the moment the tooltip appeared, so typing with the mouse resting over a
  symbol would go nowhere.

## Related

- [0547](0547-make-the-active-document-follow-keyboard-focus-into-its-editor.md),
  which makes the active document follow keyboard focus. The tooltip has to hand
  focus back for that to keep pointing at the editor the user was in.
