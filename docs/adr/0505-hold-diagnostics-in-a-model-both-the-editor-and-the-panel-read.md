# 0505. Hold diagnostics in a model both the editor and the panel read

Status: proposed
Date: 2026-08-25

## Context

`LspClient` parses `textDocument/publishDiagnostics` and, until now, threw the
result away — the comment at its notification-handling site named diagnostics,
log lines and telemetry as three things a server volunteers with "nowhere to
go yet". Giving diagnostics somewhere to go means two different readers need
them at once: the editor draws a squiggle under a range and a tooltip on
hover, in the coordinates it already draws in, and a Problems panel lists
every open document's diagnostics by file, in the coordinates the protocol
sent them in. Those are not the same shape — one is offsets into a
tab-expanded line, the other is a raw line and a UTF-16 column — and the
codebase already draws exactly this split once, for semantic colouring:
`SemanticToken` in server coordinates against `SemanticOverlay` in editor
coordinates.

The two readers also disagree about what they may do to reach the data. The
editor already reads `LanguageServices` through a client. A panel does not,
and should not gain the ability to: [0022](0022-i-3-avoid-singletons.md)'s
argument against a global accessor applies in miniature to any type that
lets a second, unrelated caller reach into a language server — `LspClient::Drain`
and `LanguageServices::TakeAnswers` are already documented as having exactly
one caller, `Poll`, because a second drain point makes an answer vanish at
random depending on which caller runs first in a frame. A Problems panel
built to drain `LanguageServices` itself would either duplicate that
single-caller discipline badly or break it.

Frame order matters too. `GraphiteLayer` already runs
`PreviewSemantics::Tick` immediately after `m_LanguageServices.Poll()` and
before any panel draws, with the same stated reason: whatever is fed into a
model at that point is stamped with the current frame's number before
anything reads it. `DocumentWindows::Render`, which drains the ticketed
answers `Poll` collected, runs after the panels in `GraphiteLayer`'s draw
order. A diagnostics model fed from inside `DocumentWindows` — the obvious
place to put it, since that is where documents and buffers already live —
would be read by the Problems panel one frame before it was written on every
frame a diagnostic arrived, so the squiggle and the panel row would disagree
for exactly that frame.

`Drain`'s existing shape is two out-parameters, `std::vector<Answer>&` and
`ServerStatus&`, because neither a status update nor — now — a diagnostic
publication carries a `RequestId` to travel on `Answer`'s own path. A third
out-parameter for diagnostics is the parameter list [0034](0034-i-23-keep-the-number-of-function-arguments-low.md)
names directly: a compound value passed as individual elements, growing once
per unticketed message kind.

## Decision

`DiagnosticsModel` (`Graphite/src/Lang/Diagnostics/DiagnosticsModel.h`) holds
every open document's diagnostics in the coordinates the protocol sent them
in — a path, a `DocumentVersion`, and a `std::vector<Diagnostic>` each with a
line and a UTF-16 column. It has no ImGui, no `Core::TextBuffer`, no tab
width and no opinion about drawing. `ProblemsPanel` reads it directly.
`DiagnosticOverlay` (`Graphite/src/Editor/DiagnosticOverlay.h`) holds the same
information for one document, converted once when an update lands into
offsets into that document's tab-expanded lines, which is what the editor
slices to draw a squiggle. The panel never sees a `DiagnosticOverlay`, and the
editor's tab-width arithmetic never reaches `Lang`.

`DiagnosticsModel` lives in `Lang/`, not `Editor/` and not `Panels/`: not
`Editor/` because `ProblemsPanel` has nothing to do with the editor and must
not pull it in — the same discipline `ChangesPanel.h` states for itself,
declining `DocumentWindows.h` because it "pulls in most of the editor"; not
`Panels/` because the model outlives every panel and is owned by none of
them, `GraphiteLayer` is. This is the placement ADR 0491 reached for
`GitModel`: refresh-driven state that more than one panel reads sits beside
the subsystem that produces it, not inside any one of its readers.

`LspClient::Drain` takes a single `Drained&` out-parameter —
`{ Answers; Status; Diagnostics; }` — replacing the two-out-parameter form,
under [0034](0034-i-23-keep-the-number-of-function-arguments-low.md)'s rule
against a parameter list that grows one element at a time for a compound
value. `LanguageServices::TakeDiagnostics(DiagnosticsUpdate&)` is the single
new exit point past `LspClient`, shaped as three vectors in one struct
rather than three separate calls:

- `Published`: one document's diagnostics as last published, paired with the
  `DocumentVersion` the server was answering about.
- `Versions`: documents whose sent version moved since the last drain — a
  `didOpen`, a `didChange`, or a restart that reset the numbering.
- `Closed`: documents no server holds open any more.

All three travel together because a set of diagnostics and the version it is
measured against are one fact; delivering them on separate calls would create
a frame in which a reader could see one without the other.

`LanguageServices::Poll` is the only caller of `LspClient::Drain`, exactly as
it already is the only caller for `Answer`s and `ServerStatus` — the
single-drain-point argument already recorded for `TakeAnswers` extends
verbatim to diagnostics, and `TakeDiagnostics` is, like `TakeAnswers`, the
only way anything outside `LanguageServices` sees the result. There remains
no accessor to a raw `LspClient`, so `ProblemsPanel` has no path to drain one
even if it wanted to; `DiagnosticsModel` is structurally the only thing it can
read.

`GraphiteLayer` owns `DiagnosticsModel` and feeds it immediately after
`m_LanguageServices.Poll()`, in the same slot `PreviewSemantics::Tick`
already occupies and for the same reason: before any panel or document
window draws, so what they read this frame was written this frame.

```cpp
m_LanguageServices.Poll();
m_LanguageServices.TakeDiagnostics(diagnostics);
m_Diagnostics.Apply(diagnostics);
```

`DiagnosticsModel::Apply` applies `Closed`, then `Versions`, then
`Published`, in that fixed order — a restart carries a close and the version
that replaces it in one update, and applying the version first would attach
it to a document the close then forgets.

## Rationale

Two readers wanting the same fact in two different coordinate systems is the
situation `SemanticToken`/`SemanticOverlay` already solved once; repeating
that split rather than inventing a third shape keeps the codebase with one
answer to "server coordinates vs. drawn coordinates", not two. Placing the
shared model in `Lang/` rather than in either reader's own directory is what
lets the panel avoid depending on the editor and lets the editor avoid
depending on `Panels/`, and it puts diagnostics beside the client that
produces them rather than inside whichever reader happened to be built first.
The frame-ordering constraint is not a style preference: feeding the model
after the panels draw is a bug that only shows up on the exact frame a
diagnostic changes, which is precisely the frame a test with no server
running will never construct.

## Consequences

### Positive

- `ProblemsPanel` takes `const DiagnosticsModel&` and nothing else related to
  diagnostics; it structurally cannot drain a language server, so the
  single-drain-point discipline `LanguageServices` already relies on for
  `Answer`s cannot be broken by a panel written later without touching
  `LanguageServices` itself.
- `Drained` and `DiagnosticsUpdate` both fix the parameter-list growth
  [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) warns about:
  a fourth unticketed message kind is one more field on an existing struct,
  not a fourth out-parameter threaded through `Drain` and `Poll`.
- The editor and the panel can each be tested against `DiagnosticsModel` or
  `DiagnosticOverlay` alone, with no window and no process, the same way
  `SemanticOverlay` already is.

### Negative

- Two models exist for one fact. A reader has to know which one applies to
  which caller — `Lang/Diagnostics` for anything that is not drawing, editor
  coordinates only inside `DiagnosticOverlay` — rather than there being one
  place "diagnostics" live.
- `DiagnosticsModel::Apply`'s three-phase order is a correctness requirement
  that is invisible at any call site; get the order wrong and the only
  symptom is a restart that leaves a stale entry alive, which nothing but the
  model's own tests exercises directly.

### Neutral

- `DiagnosticsModel` holds entries for every document a server has opened,
  including ones opened by a preview pane or by `LanguageServices`'s own
  warm-up, not only ones with an editor tab. Filtering that down to "what has
  a tab" is the caller's job, not the model's; see
  [0506](0506-dim-a-diagnostic-the-buffer-has-outrun-rather-than-dropping-it.md)
  and the model's own `Total` for how that filter is shared rather than
  duplicated.

## Alternatives Considered

### Feed the model from inside `DocumentWindows::Render`

Keeps diagnostics next to the buffers and documents that already live there,
and avoids adding a member to `GraphiteLayer`. Rejected because
`DocumentWindows::Render` runs after the panels draw in `GraphiteLayer`'s
frame, so a model fed there would be read by `ProblemsPanel` one frame stale
on every frame a diagnostic actually changed — invisible to any test, visible
to anyone typing while `clangd` is re-parsing.

### Let `ProblemsPanel` hold a reference to `LanguageServices` and drain it itself

Removes `DiagnosticsModel` entirely; the panel would call something shaped
like `TakeDiagnostics` on its own draw path. Rejected on the single-drain-point
argument already established for `TakeAnswers`: `Poll` must drain every
client to notice a dead process, so it must run first, and a second drain
point racing it makes a diagnostic vanish depending on which caller the frame
happened to schedule first — the same failure shape `LanguageServices.h`
already calls "indistinguishable from a server that occasionally forgets to
answer."

## Related

- [0022](0022-i-3-avoid-singletons.md) — the single-caller discipline this
  record extends from `Answer`s to diagnostics.
- [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) — the rule
  `Drained` and `DiagnosticsUpdate` both apply.
- [0485](0485-take-code-intelligence-from-external-language-servers.md) — the
  decision that puts a language server, and therefore its diagnostics,
  behind `LspClient` at all.
- [0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) — the
  placement precedent: shared, refresh-driven state lives beside the
  subsystem that produces it, not inside any one reader.
- [0506](0506-dim-a-diagnostic-the-buffer-has-outrun-rather-than-dropping-it.md)
  — what the model does with a diagnostic the buffer has since outrun.
- `Graphite/src/Lang/Diagnostics/DiagnosticsModel.h` and
  `Graphite/src/Editor/DiagnosticOverlay.h` — the two shapes this record
  places.
