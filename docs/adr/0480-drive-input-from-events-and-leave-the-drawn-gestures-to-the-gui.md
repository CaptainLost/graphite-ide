# 0480. Drive input from events and leave the drawn gestures to the GUI

Status: accepted
Date: 2026-08-13

## Context

Nothing carried input. The window polled GLFW and threw the results away; `Layer`
had no way to hear about anything; and every screen read the keyboard for itself
at the moment it drew, through ImGui. Sixty-five such reads sat in ten files,
four of them inside the engine, which therefore knew ImGui's key codes.

Without a way for one screen to say "I took that keystroke", the shortcut
capture screen had to be asked. `Panel` grew two virtuals for it —
`IsCapturingInput()` so the router could be told to stand down, and
`OnNotDrawn()` so a capture behind a hidden tab could be ended before it
swallowed every shortcut in the application. Both sat in the base class, so
every panel paid for one panel's problem.

## Decision

Input arrives as events and is consumed on the way down:

```
GLFW callback → GlfwWindow → Application::OnEvent
                               ├ ImGuiRenderer  — takes it if a field wants typing
                               └ layers, top of the stack first, stop at Handled
                                    └ panels that drew → shortcut router → editor queue
```

- `Core/Events` holds `Event`, `EventDispatcher` and the window, key and mouse
  events. `Event::Handled` stops the walk.
- `Window::SetEventCallback` is installed **before** the GUI backend
  initialises. That backend installs callbacks of its own and chains to whatever
  it finds, so ours have to be there first or they are replaced rather than
  chained.
- `PanelManager` delivers only to panels whose body ran on the previous frame. A
  panel that is hidden, collapsed or behind another tab of the same dock node is
  not on screen for the user to have aimed at.
- The keys a panel and the router both leave alone are queued for the frame, and
  the focused editor drains the queue while it draws.

Three things stay with the GUI, and the rule for all three is the same: **a
gesture whose meaning depends on what it is aimed at belongs where that is
known, which is the draw.**

- **The wheel.** Which editor it was pointing at is known nowhere else.
- **Selection dragging**, and the modifiers read during it.
- **Which editor listens.** `ImGui::IsWindowFocused()` on a document's own child
  window decides both that and where the caret blinks, deliberately from one
  variable — see the comment in `TextEditorWidget`. Moving the keyboard out of
  the draw would leave those two reading different sources.

There is no `Input` polling class. The vocabulary such a class would bring —
`KeyCode`, `MouseCode`, `KeyModifier` — exists in `Core/Base` and is what the
events carry.

## Rationale

Consumability is the whole point. Once a keystroke can be taken, the questions
the base class used to ask answer themselves: the capture screen takes the chord
it is recording and the router never sees it, so `IsCapturingInput()` and
`OnNotDrawn()` are gone.

The GUI decides whether it wants a keystroke by `io.WantTextInput`, not by
`WantCaptureKeyboard` and not by "some item is active". Both of those were tried
and both were wrong in ways worth recording, because both look correct:

- `WantCaptureKeyboard` is true whenever keyboard navigation is active, and
  `ImGuiConfigFlags_NavEnableKeyboard` makes that true for any focused window
  that allows navigation. It means "the GUI could navigate", not "the GUI is
  using this key", and consuming on it swallowed every keystroke in the
  application.
- "Some item is active" is true for as long as a mouse button is held over a
  widget, so it stopped the keyboard working for the whole of a selection drag.

An `Input` class was measured before being written: three sites in the whole
repository poll input state, and all three are inside draw code and want state
coherent with the frame being drawn rather than the platform's raw state. It
would have had no callers.

## Consequences

### Positive

- `Panel` lost both compensating virtuals; a panel consumes or it does not.
- No engine file reads ImGui key codes for application input any more, and the
  engine draws no application screen — the shortcuts screen moved to
  `Graphite/src/Panels`.
- Modifiers are those of the keystroke rather than of the frame it was noticed
  in, which is what makes the AltGr rule (`Ctrl` held and `Alt` not) exact.
- A shortcut can be a tap of modifiers alone, which the old draw-time polling
  could not express.

### Negative

- **The editor's input path has no automated coverage.** Nothing in either suite
  exercises typing, selection or undo, so `979d2cb` was judged by hand. It is
  isolated in one commit for exactly that reason.
- A keystroke consumed by a panel or the router no longer also reaches the
  editor. No binding collides today, and a future one would now shadow rather
  than double-fire — a change, and arguably the better behaviour, but a change.
- Two mechanisms now carry input rather than one, and which applies is a
  judgement about the gesture. The rule above is what keeps that from being
  arbitrary.

### Neutral

- `EventBus` is untouched and unrelated: it carries domain facts between panels
  (a folder opened, files changed) and has no notion of consumption.

## Alternatives Considered

### Leave shortcuts on ImGui's routing

`ImGui::Shortcut` with `ImGuiInputFlags_RouteGlobal` arbitrates per widget, per
window and per popup — finer than anything reasonable to hand-roll. Rejected
because it could not be reconciled with the capture screen without the two
virtuals this record removes, and because it left the engine holding ImGui key
codes. The arbitration lost is real, and the wheel and the focus gate are where
it was kept.

### Move the editor's keyboard out of the draw entirely

The faithful version of the event model. Rejected: the focus gate and the caret
would then read different sources of truth, which is precisely what the existing
code says it avoids on purpose.

## Related

- [0479](0479-settle-one-house-style-for-naming-formatting-and-layout.md) — the
  refactor this arrived during.
- [0002](0002-keep-core-free-of-ide-knowledge.md) — the shortcuts screen leaving
  the engine finishes what that record asks for.
