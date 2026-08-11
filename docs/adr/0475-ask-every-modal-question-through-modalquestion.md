# 0475. Ask every modal question through ModalQuestion

Status: proposed
Date: 2026-08-11

## Context

The application asks the user five questions through modal popups: whether to
open a large file, what to do about a file that changed on disk under a dirty
tab, whether to overwrite a file changed since it was opened, whether to save a
dirty tab being closed, and whether to lose unsaved work on exit or on closing
the workspace.

Every one of them is opened with an empty `BeginPopupStack`, so they all compete
for ImGui's popup level 0. Opening a second popup there does not stack it:
`OpenPopupEx` calls `ClosePopupToLevel` and tears the first one down
(`imgui.cpp`). The level is a resource with exactly one slot, but not one
[0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
answers: what has to survive losing the race for it is not a handle but the
*question*, which must still be asked afterwards.

`ModalGate` was written for that: one question owns the level at a time, and a
question that cannot be asked now is retained by its owner and asked once the
gate frees. Around it, each of the five sites wrote out the same three steps by
hand:

```cpp
if (requestFlag && gate.TryOpen(title))     // open when the gate allows it
if (ImGui::BeginPopupModal(title, ...))     // draw the body
else if (gate.HoldsFor(title) && !ImGui::IsPopupOpen(title))   // and the backstop
```

The third step is the one that matters. A popup can go away without being
answered — Escape, or something opening over it — and the per-site state that
identifies the question stays set. That state is what guards the mechanism
behind the question, so each way of getting it wrong wedges something for the
rest of the session:

- a destroyed reload question silences every later "changed on disk" question,
  for every document;
- a destroyed overwrite question makes Ctrl+S on that document do nothing at
  all, silently;
- a destroyed close question leaves the tab, and before the queue existed the
  window itself, unclosable.

Nothing in the build or the tests reports any of that. It held because the
author wrote the same backstop correctly five times, and would have stopped
holding the first time a sixth question was added and the third step forgotten —
which is the ordinary outcome, since the first two steps are enough to make a
question that works when the user answers it.

Three of the five had also grown a queue, because a second question raised
before the first resolves must be asked afterwards rather than dropped, and one
had grown a filter applied when a question comes off that queue. So the
duplicated ceremony was not three lines by then; it was three lines plus a queue,
a pop guard against a busy gate, and an "active" slot, repeated with small
variations that had to be read carefully to tell apart.

## Decision

Every modal question in the application is asked through `ModalQuestion`, a
class template beside `ModalGate` holding the queue of what still has to be
asked, the payload of what is on screen, and the whole ceremony:

```cpp
if (question.Begin())
    // draw the body; whichever button answers calls question.Answer()
```

- No site hand-rolls `TryOpen`/`OpenPopup`/`BeginPopupModal`/`IsPopupOpen`, and
  no site writes the dead-popup backstop: `Begin`'s false path performs it for
  every question there is, releasing the gate and dropping the payload.
- `Ask` queues. Questions are asked one at a time, in order, and never dropped
  or replaced — including a request made while the gate is held by some other
  question.
- Nothing is taken off the queue while the gate is busy, and a candidate the
  gate then refuses anyway goes back to the front rather than being discarded.
- `Begin` takes an optional predicate applied when a question comes off the
  queue, for the case where what it was about may have been dealt with while it
  waited.
- The payload names its subject in a way that survives frames — a path, not an
  index — and is looked up again when the answer arrives.
- `Answer` releases the gate before `ImGui::CloseCurrentPopup`, so a question
  waiting its turn opens in the same frame.
- `ImGuiWindowFlags_AlwaysAutoResize` is baked in, because all five questions
  use exactly that and nothing else. A flags parameter is added when a sixth
  question needs one, not before.

## Rationale

The backstop is a correctness requirement that was being met by repetition, and
repetition is the one way of meeting a requirement that degrades every time the
code is touched ([0174](0174-es-3-dont-repeat-yourself-avoid-redundant-code.md)).
Moving it inside a type makes it structural: a question drawn through `Begin`
cannot be missing it, and a new question gets it without its author knowing it
exists. That is exactly the trade
[0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
describes — the messy construct is ImGui's single popup level and the reasoning
around it, and it now sits in one file instead of being spread across two.

The five sites also stopped being comparable to each other. Each was a hundred
lines mixing what the question asks with how a question is asked, which is two
logical operations in one place
([0439](0439-f-2-a-function-should-perform-a-single-logical-operation.md)) and
what made `Render` long enough to hide the differences between them
([0440](0440-f-3-keep-functions-short-and-simple.md)). What is left at each site
is the body of the question and what its buttons do.

## Consequences

### Positive

- The dead-popup backstop is written once and cannot be forgotten. Adding a
  sixth question cannot reintroduce any of the three wedging failure modes.
- Queueing, the pop-time guard against a busy gate, and the "never drop a
  question" rule are one implementation rather than three similar ones.
- Twelve fields across two classes become five members that say what they are, and
  the four popup title constants move from an unnamed namespace in a `.cpp` to
  private class members, which is where the questions that bind to them can see
  them ([0385](0385-sf-21-dont-use-an-unnamed-anonymous-namespace-in-a-header.md),
  [0021](0021-i-2-avoid-non-const-global-variables.md)).
- The five sites are now visibly the same mechanism with different bodies, so a
  difference between them — the close question's pop-time filter, the exit
  question's refusal to close on a failed Save All — reads as a deliberate
  choice rather than as one of several things that might have been an oversight.

### Negative

- It is a class template, so its whole implementation lives in a header and is
  compiled into every translation unit that asks a question. The two that do
  are the two that already included ImGui, but the cost is real and grows with
  each distinct `Payload`.
- A caller can still forget `End()`. The type mirrors ImGui's own Begin/End
  convention deliberately — it is what a reader of this codebase expects, and
  an RAII wrapper would have to solve returning "false, do not draw" — but the
  convention buys nothing against the mistake it is famous for.
- Reading one question now means reading two files. The ceremony is no longer
  in front of the person changing the body, which is the point, and is also how
  a caller comes to hold an assumption about `Begin` that the header does not
  actually promise.
- Site A changes behaviour: a second large file picked while the size question
  is up used to overwrite the pending one, silently dropping the first file.
  Both are now asked about, in order. This is deliberate — it is what the reload
  and close questions already did, and the old behaviour was a bug nobody had
  reported — but it is a change no user asked for.
- `Asked` requires `operator==` on the payload. It is only instantiated where it
  is used, so a payload without one is still a valid payload, but that is a
  constraint the class comment has to carry rather than the signature.

### Neutral

- `ModalGate` is unchanged. It still answers "who owns the level", and
  `ModalQuestion` is now its only caller.
- Nothing about where the questions are drawn changes: they still sit outside
  any `Begin`/`BeginChild` pair, which is an ImGui requirement and not something
  this type could have absorbed.
- The five payload types are chosen per site; the template says only that a
  payload is movable and should survive frames.

## Alternatives Considered

- **Leave the five copies and rely on review.** No new abstraction, and each
  site stays readable in isolation. Rejected because the thing being reviewed is
  a branch that never runs in normal use: the backstop only matters when a popup
  is destroyed, which no one does on purpose, so its absence looks exactly like
  correct code until a user loses a session to it.
- **A non-template base class with a type-erased payload.** One compiled
  implementation instead of one per payload, and no template in a header.
  Rejected because every site would then cast its payload back out, which trades
  a compile-time cost for a runtime mistake — and the payloads are a path, a
  path, a path, a small struct and an enum, so the erasure buys nothing but
  indirection.
- **An RAII scope object instead of Begin/End.** It would make `End()`
  impossible to forget. Rejected for now because the object has to be
  constructible in the "no question on screen" state and tested for it, so the
  call site becomes a scope holding an object that may be inert — which is
  further from what ImGui code in this repository looks like than the
  obligation is worth. If a site ever does forget `End()`, this is the thing to
  reconsider.

## Related

- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — the construct being encapsulated is ImGui's single popup level.
- [0174](0174-es-3-dont-repeat-yourself-avoid-redundant-code.md) — five copies of
  a correctness backstop is the case this rule is about.
- [0439](0439-f-2-a-function-should-perform-a-single-logical-operation.md) and
  [0440](0440-f-3-keep-functions-short-and-simple.md) — why `Render` had to
  shrink.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — the payload is constrained by `std::movable` and the filter by
  `std::predicate`, rather than being documented in prose.
- [0385](0385-sf-21-dont-use-an-unnamed-anonymous-namespace-in-a-header.md) and
  [0021](0021-i-2-avoid-non-const-global-variables.md) — why the titles became
  private static members rather than following the questions into the header as
  they were.
- [0068](0068-c-47-define-and-initialize-member-variables-in-the-order-of-member-declaration.md)
  — each question holds the gate by reference, so the gate is declared first.
