# 0506. Dim a diagnostic the buffer has outrun rather than dropping it

Status: proposed
Date: 2026-08-25

## Context

A diagnostic describes a specific version of a document's text: `clangd`
answers `textDocument/publishDiagnostics` against whatever it last received
through `didChange`, and the buffer keeps moving the instant the user types
again. Between a keystroke and the next reply, what the model holds is
strictly older than what is on screen. Something has to decide what a reader
sees during that gap, and the codebase already answered this question once,
for a different kind of server answer.

[0489](0489-open-a-previewed-file-with-the-language-server.md) settled it for
semantic colouring: a stale `SemanticOverlay` — one whose revision or tab
width has moved past what it was computed for — is dropped outright rather
than shifted onto the new text, because "a moment with no semantic colours
costs nothing a reader notices; a semantic colour on a word it no longer
describes is a wrong answer dressed as a right one." `SemanticOverlay` also
had a second mechanism for staying current — `EditSinks::Semantic`, which
shifted spans line-by-line as the buffer was edited, so that a colour rode
along with its identifier between replies. This branch removes that
mechanism entirely: `SemanticOverlay::Set` still replaces everything on a
fresh reply, but nothing shifts an existing overlay for an edit any more, so
between replies a stale colour simply keeps its old position — the drop
policy is what makes that honest, rather than showing a colour that has
silently slid onto the wrong token.

Nothing tracked, before this branch, told a caller whether a set of server
data was stale in the first place except by revision comparison at the point
of use. Diagnostics need the same comparison, against a currency the codebase
did not yet have: `LspClient::OpenDocument` and `ChangeDocument` sent a
version number to the server on every call but never returned it, so nothing
outside `LspClient` could name the version an answer was measured against.
Reusing the editor's own revision counter for this was available and wrong:
that counter counts edits to the buffer, this needs to count what has been
sent to the server, and the two fall out of step the instant a `didChange` is
debounced, dropped for a document the server was never told about, or a
server restarts and resets its own numbering to 1.

## Decision

`DocumentVersion` is a strong type in `Graphite/src/Lang/Api/LanguageService.h`,
next to `Utf16Column`, for the same reason that header already gives for
wrapping a number: it is trivially confused with the editor's own revision
counter, which counts something else entirely. `0` means "never sent", and is
also what a reply naming no version reads as — `DocumentVersion`'s comparison
operators are defaulted, so `0` compares less than every real version and
therefore always reads as stale once anything has been sent.

`LspClient::OpenDocument` and `ChangeDocument` return the `DocumentVersion`
they just sent, rather than remaining `void`. `LanguageServices` records what
came back per document and emits a `DocumentVersionNote` whenever it moves —
a `didOpen`, a `didChange`, or a restart that resets the count to 1.
`DiagnosticsModel::Entry` keeps two fields, `Sent` and `Answered`, and a
`HasAnswer` flag, rather than a single staleness bit: staleness is a
comparison made when it is asked for, in `IsStale`, not a flag written once
at delivery time and left to rot — a set that is fresh the moment it lands is
stale again on the very next keystroke, so nothing about it can be decided in
advance and cached.

`HasAnswer` exists because `DocumentVersion{}` is deliberately overloaded to
mean two different things — "never sent" and "a reply that named no
version" — and those two must read oppositely. A document nothing has ever
answered is not stale, having nothing yet to be stale about; a document a
server did answer, but without naming a version, is stale as soon as
anything has been sent, which is the conservative reading `DocumentVersion`'s
own comment already commits to. Comparing `Answered` against `Sent` without
`HasAnswer` cannot tell these apart, because both produce `Answered ==
DocumentVersion{}`; `IsStale` checks `HasAnswer` first and returns `false`
immediately when it is unset, before the version comparison ever runs.

A stale diagnostic is dimmed, not dropped. `DiagnosticOverlay` keeps drawing
the squiggle at its last-known position — there is no `Shift`, deliberately;
see below — and `DiagnosticsModel::IsStale`, read by both the editor and the
Problems panel, is what tells each of them to reduce it to the dimmed
`EditorColor::DiagnosticError`/`Warning` alpha instead of hiding it.

**`SemanticOverlay`'s line-shifting is gone, and no diagnostics equivalent
was built.** `EditSinks::Semantic` and its call site are removed in this
branch; semantic colours now simply stay where the last reply put them until
a fresh reply replaces them, exactly as `DiagnosticOverlay` already does for
diagnostics — both classes carry the same comment: "There is deliberately no
way to adjust an overlay for an edit... shifting one line-by-line only ever
approximated an answer that was already on its way." Diagnostics were never
going to get their own shifting mechanism even before this simplification;
`DocumentVersion`-based dimming is the entire answer to staying current
between replies, for both overlays now.

## Rationale

ADR 0489's drop-outright policy is right for a colour and wrong for a
message. A dropped colour costs a reader nothing they would notice — the
token is simply drawn in its plain, unclassified shade for a moment. A
dropped diagnostic costs the reader the actual information: whether the file
compiles. Dropping it on every keystroke and having it reappear on the next
reply would make a broken file blink between "broken" and "fine" continuously
while someone is mid-edit, which is a worse experience than showing them a
possibly-outdated error and letting them judge for themselves whether their
edit fixed it. Dimming keeps the information and marks it provisional — the
correct treatment for content that carries meaning rather than shade, which
is exactly the distinction ADR 0489's own trade rested on and did not need to
generalise past colour.

Not building a diagnostics equivalent of `EditSinks::Semantic` follows from
freshness rather than from omission: a dimmed squiggle at a position an edit
has since moved is openly describing older text, which is what shifting was
trying, and failing, to paper over. Once dimming exists, shifting has nothing
left to earn its keep — it was never removing the possibility of describing
the wrong token, only disguising it for longer.

## Consequences

### Positive

- A diagnostic never disappears and reappears within the same editing
  session unless the underlying error genuinely comes and goes; a reader
  fixing a bug watches the same squiggle dim rather than watching it vanish
  and hoping it does not come back.
- `IsStale` is one comparison read fresh every time, so there is no cached
  flag anywhere that can go stale itself relative to the fields it was
  computed from.
- Removing `EditSinks::Semantic` deletes a whole mechanism — the shift
  logic, its call site, and the three tests that pinned it — rather than
  adding a second, diagnostics-flavoured copy of it.

### Negative

- A dimmed squiggle can sit at a position genuinely wrong for the current
  text — an edit above the line has changed the line numbering the server
  last knew about — for as long as it takes the next reply to arrive. This
  is visible and is the accepted cost of not shifting; a reader has to trust
  the dimming, not the position, once something is dimmed.
- `DiagnosticsModel::Entry` carries three fields (`Sent`, `Answered`,
  `HasAnswer`) to express what a single boolean might have looked like it
  could hold; a reader has to understand why `HasAnswer` exists at all rather
  than trusting `Answered == DocumentVersion{}` to mean "no answer yet."
- A `didChange` goes out during `DocumentWindows::Render`, after that frame's
  drain, so its version note is only picked up by the next frame's
  `TakeDiagnostics` — dimming is at most one frame late. Invisible in
  practice, but it is a real gap between "sent" and "known sent."

### Neutral

- `RestartServer` resetting every document's version to 1 is what makes
  `Closed` and a fresh `DocumentVersionNote` both necessary on a restart: a
  model comparing an old high version against the new low one would
  otherwise read a set describing a dead process as fresh forever. This
  record documents the freshness mechanism that makes that failure avoidable;
  the restart path itself is unchanged by this record.

## Alternatives Considered

### Drop a stale diagnostic outright, following ADR 0489 verbatim

Keeps one policy for every kind of overlay staleness, with no special case.
Rejected per the Rationale: ADR 0489's argument is explicitly about the cost
of being wrong, and that cost is asymmetric between a colour and an error
report. Applying the same policy here would make a file's error state flicker
on every keystroke, which is worse than showing a possibly-stale one.

### Build a shift mechanism for `DiagnosticOverlay`, the way `EditSinks::Semantic` did for `SemanticOverlay` before this branch

Would keep a diagnostic's drawn position tracking the buffer between replies
instead of freezing it. Rejected because it repeats the exact approximation
this branch is removing from semantic colouring for the same reason: a
shifted span is a guess about what an edit did to a position the server has
not confirmed, and once dimming already tells the reader "this may not be
current," a guessed position adds risk without removing the need for a fresh
reply to actually be trusted.

## Related

- [0489](0489-open-a-previewed-file-with-the-language-server.md) — the
  drop-outright policy for stale semantic colours this record departs from,
  and the reasoning for it that this record narrows to "content that is
  shade, not message."
- [0505](0505-hold-diagnostics-in-a-model-both-the-editor-and-the-panel-read.md)
  — where `DiagnosticsModel` and `DiagnosticOverlay` live and how they are
  fed; this record covers what they do once fed.
- `Graphite/src/Lang/Api/LanguageService.h` — `DocumentVersion`.
- `Graphite/src/Lang/Diagnostics/DiagnosticsModel.h` — `Entry`, `IsStale`.
- `Graphite/src/Editor/DiagnosticOverlay.h` and
  `Graphite/src/Editor/SemanticOverlay.h` — the matching "no `Shift`"
  comments this record explains.
