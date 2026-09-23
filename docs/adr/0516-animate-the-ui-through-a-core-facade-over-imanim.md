# 0516. Animate the UI through a Core facade over ImAnim

Status: proposed
Date: 2026-09-23

## Context

Every state change in the interface was a snap: a button's fill appeared the
frame the pointer arrived and vanished the frame it left, and the same held for
row highlights, scrolling to a jump target, popups and the caret. The aim is an
interface that feels finished, added one animation at a time and judged by eye
after each.

Nothing in the codebase tweens anything; the caret blink is the only
time-based drawing, and it is a square wave on `ImGui::GetTime()`. The frame
loop polls rather than waits, with vsync, so a frame is drawn every refresh
whether or not input arrived — an animation needs no extra wake-ups to keep
moving.

## Decision

Adopt [ImAnim](https://github.com/soufianekhiat/ImAnim) as a submodule in
`vendor/imanim`, compiled into the existing `ImGui` static library rather
than a project of its own.

Nothing outside `Core/ImGui/Animation.cpp` includes `im_anim.h`. The rest of
the application asks `Core::Animation` for a value — `HoverAmount(id,
hovered)` is the first — and that facade owns the durations, the easing and the
tween policy. `Core::Animation::BeginFrame` advances every tween;
`ImGuiRenderer::BeginFrame` calls it straight after `ImGui::NewFrame`. It does
not call `iam_gc`, which ages a tween from the frame it was created rather than
the frame it was last used: at 240 Hz its default limit drops a hover still in
use after 2.5 s, and the item under the pointer fades out and back in.

Only hovering fades. A press answers in the frame it happens: feedback on a
click that arrives late reads as lag, not polish.

## Rationale

ImAnim is two files, MIT, with no dependency beyond ImGui, and its API is
immediate-mode — a call per frame returns the current value — so it fits the
way every widget here is already written. The facade exists so the timing is
decided once: the hover of a title-bar button and of a find toggle feel the
same because they cannot be given different numbers, and a later "reduce
motion" setting has one place to take effect.

## Consequences

### Positive

- An animation is a call and a blend at the drawing site, with no state kept
  by the widget.
- The durations and the easing are named in one header and tested there.

### Negative

- A tween reports its starting value in the frame its target changes and
  moves from the next, so every animation starts one frame late. At 60 Hz
  that is 16 ms and not visible, but a test that checks the first frame of a
  fade has to step one frame past the change.
- ImAnim keeps its tweens in globals keyed by ImGui id, outside the ImGui
  context. Two items sharing an id share a fade.
- Without `iam_gc` nothing is ever forgotten. A tween is only created for an
  item that has been hovered at least once, so the growth is bounded by what
  the pointer has touched, but a later animation that creates one per frame or
  per row scrolled past has to bring its own cleanup.
- Only widgets drawn here can be animated. Rows drawn by `ImGui::Selectable`
  and `TreeNodeEx` take their hover colour from the style, which ImGui applies
  itself; fading those means drawing the row background here instead.

### Neutral

- The `ImGui` library now also carries ImAnim; it builds with the same
  (default) warning level as ImGui, outside Core's fatal warnings.

## Alternatives Considered

- **A hand-written tween in Core.** A single exponential approach per id is a
  few lines and would have served the first animation. It lost because the
  steps that follow — smooth scrolling to a jump target, popups easing in, a
  pulsing caret — each need their own easing curve and timing, and writing
  those one by one is writing ImAnim again.
- **Calling ImAnim directly from each widget.** Fewer lines today, but every
  call site would pick its own duration, easing and policy, and a setting that
  turns animation off would have to reach all of them.

## Related

- [0001](0001-record-architecture-decisions.md)
