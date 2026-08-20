# 0489. Open a previewed file with the language server

Status: proposed
Date: 2026-08-20

## Context

[0485](0485-take-code-intelligence-from-external-language-servers.md) put
every semantic answer behind a server that speaks per-file `didOpen` and
`didClose`. Until now, the only thing that ever sent those was an editor tab:
opening a file for editing opened it with the server, closing the tab closed
it there too. Search results, find-references and go-to-definition all show a
preview pane — a read-only view of a file, sometimes one the user has no tab
for — and that pane drew with lexer colours only, because nothing told the
server the file existed.

That is a visible gap once a document is coloured semantically and a preview
next to it is not: the same identifiers, the same language, one of them shows
what it is and the other shows only what it looks like. Looking at a file
through a preview is not editing it, but it is still a reason to want the
answer a language server gives — the pane is what the user is reading right
now.

Reaching that answer from a preview raises three questions an editor tab never
had to: how many times is one file opened if two panes show it at once, when
is it closed given that a pane's visibility is driven by ImGui and not by an
edge-triggered event, and what happens to a request that is still outstanding
when the pane it was for stops existing.

## Decision

A preview pane declares the file it shows, once a frame, to `PreviewSemantics`
— a mediator sitting behind the abstract `PreviewServerPort`, which in
production forwards to `LanguageServices` through `LanguageServicesPort`.
`PreviewSemantics` is keyed by path and counted by declaration, not by pane:
the first declaration of a path in a frame opens the file with the server and
asks for its tokens, and every later declaration of the same path that frame
is a map lookup and a timestamp. Two preview panes showing the same file cost
one `didOpen`, one request and one overlay, the same as one pane would.

Declaration happens every frame a pane is visible, from `PreviewContent`'s
draw path, rather than being acquired once and released when the pane stops
showing the file. A collapsed or hidden panel never reaches `OnImGuiRender`,
so there is no edge to trigger a release on — an acquire-on-show,
release-on-hide scheme would leak an open document for the rest of the
session the first time a panel closed without the pane inside it running one
more frame to notice. `PreviewSemantics::Tick`, called once at the top of the
frame before any pane declares, is what actually closes an entry: anything
not declared in the frame just finished is a frame overdue, and is closed and
erased. An entry that reaches zero declarations is given exactly one frame of
grace before that happens, because a pane's selection can disappear for a
single frame when the groups behind it rebuild, and closing on the spot would
turn that blink into a full re-parse of the translation unit when the pane's
selection reappears the frame after.

A file an editor tab already holds open is never opened or closed by this
class. `Declare` takes `editorHolds`, and when it is true the entry is
created without sending `didOpen` and its `Opened` flag stays false, which is
what `Tick` and the destructor check before sending `didClose` — a preview of
a file being edited rides on the tab's own open document and must not
outlive or race it. The same flag decides whether the entry watches the
buffer's revision at all: a file this class opened cannot change under it, so
there is nothing to watch, while a file with a tab is being typed into and its
revision is what tells `Declare` the snapshot behind an outstanding request
has gone stale.

An answer is converted against the exact text its request was asked about,
kept as `Entry::Snapshot`, not against whatever the buffer holds when the
reply lands — the server's columns describe the text at the moment of the
request, and applying them to text that has since moved would put a colour on
the wrong word. `Apply` matches the answer's ticket against the entry's
outstanding one and drops it silently if they no longer agree, which happens
when a second request superseded the first before the first's answer arrived.
Staleness — a revision that moved, a tab width that changed — drops the
overlay outright rather than trying to shift it onto the new text: a moment
with no semantic colours costs nothing a reader notices; a semantic colour on
a word it no longer describes is a wrong answer dressed as a right one.

The definition-choices popup in `DocumentWindows::RenderDefinitionChoices`
deliberately keeps lexer colours only and is not routed through
`PreviewSemantics`. It lists one line each from several candidate
definitions, drawn from wherever `clangd` says they are — none of those lines
is "the file being previewed", there is no one path to declare, and the
popup is gone again before a request could plausibly return.

## Rationale

The alternative to counting by declaration was counting by pane, which parses
the same translation unit twice the moment two panes show one file — the
exact duplicate cost [0485](0485-take-code-intelligence-from-external-language-servers.md)
already accepts once per document and this decision has no reason to pay
again. The alternative to per-frame declaration was an acquire/release pair
tied to the pane's own lifetime, which needs a moment that reliably fires on
last-hide; ImGui panels do not offer one, since a docked-and-collapsed panel's
content function is simply never called, so there is no code path left to run
the release. Declaring every frame turns "is anyone still looking at this
file" into a question `Tick` can answer for itself, from data it already has,
instead of a question every call site would have to answer correctly on
every path that can make a pane stop being shown.

## Consequences

### Positive

- A preview pane reads with the same semantic colours an editor tab gets,
  closing the gap between "editing a file" and "looking at one" that
  [0485](0485-take-code-intelligence-from-external-language-servers.md) left
  behind.
- Sharing by path means the cost of two panes on one file is the cost of one
  pane, not two — the mediator itself is what makes that true, not something
  every call site has to arrange.
- A file with a tab is untouched by this feature entirely: `editorHolds`
  keeps the tab's own open document as the only one that exists, so nothing
  here can double-open or race-close it.

### Negative

- Two preview panes on two different large files are two translation units
  parsed by `clangd`, alongside whatever the editor's own tabs already have
  open. Nothing here bounds that; it grows with however many distinct files
  are visible in preview panes at once.
- Declaring every frame is bookkeeping work — a map lookup and a stamp per
  visible preview, per frame — paid whether or not the file's colours have
  anything left to change. It is cheap next to a re-parse, but it is not free,
  and it runs whether or not the pane's view has changed at all.
- The one-frame grace period means a pane that legitimately stops showing a
  file still costs one extra frame of open document before `Tick` notices and
  closes it. Usually invisible; a pane that flickers between files rapidly
  could in principle keep re-arming the grace period and never let an entry
  close, though nothing in the current call sites does that.

### Neutral

- `RenderDefinitionChoices` staying on lexer colours is a scope line, not a
  gap: it can be revisited if the popup ever comes to show more of a
  candidate than one line, at which point "which file is being previewed"
  would have an answer again.

## Alternatives Considered

### Count by pane rather than by path

Give every preview pane its own slot in `PreviewSemantics`, keyed by whatever
identifies the pane rather than by the file it shows. Simpler to reason about
in isolation — each pane's bookkeeping is entirely its own, with no lookup
shared across panes. Rejected because two panes showing the same file would
then send two `didOpen`s and run two requests for identical tokens, parsing
the same translation unit twice for no reason a reader benefits from; and a
single shared slot for "the currently previewed file" — the sketch that
avoids that duplication without keying by path — would thrash `didOpen` and
`didClose` every frame whenever two visible panes showed different files,
since the one slot could only ever hold one of them at a time. Keying by path
is what lets the sharing happen without either cost: two panes on one file
collapse into the one entry that path already has, and two panes on two files
simply get two entries, with no slot fighting over which file it currently
means.

### Acquire on first show, release when a pane stops showing the file

Have a pane call an explicit `Acquire`/`Release` pair around the file it
shows, opening the document the first time a pane displays it and closing it
when the pane's selection changes or the pane itself closes — the ownership
model a scoped resource would normally get, and the one that needs no `Tick`
sweeping entries every frame. Rejected because a panel that is hidden or
collapsed into a dock tab never reaches `OnImGuiRender` at all, so there is no
frame in which its `Release` call would run: the document it opened would
stay open with the server for the rest of the session, with nothing left to
notice it should close. Declaring every frame turns "is anyone still showing
this file" into a question `Tick` can answer from data every visible pane
already supplied that frame, rather than a release call that depends on a
pane getting one last frame to make it in.

## Related

- [0485](0485-take-code-intelligence-from-external-language-servers.md) — the
  decision this one extends from "an editor tab opens a document" to "looking
  at a file is also a reason to".
- [0488](0488-ship-clangd-with-the-release.md) — the server this feature asks;
  nothing here changes how it is found or launched.
