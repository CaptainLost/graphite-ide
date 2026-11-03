# 0666. Count who holds each document open with a language server

Status: proposed
Date: 2026-11-03

## Context

Three parts of the application open a document with a language server: a tab,
when it is first drawn; a preview pane, in the search panel or a hover; and
the background analysis. `LanguageServices` keeps one open document per path
and counts no references, so whichever opener sent the didClose took the
document away from all of them. 0512 named that a convention with no mechanism
and deferred counting until it was settled whose text wins; 0595 has since
settled it: the open document in the store does.

Each opener kept its own flags to avoid closing what another held: the
preview "disowned" a document when a tab took it over, and the analysis read a
"held elsewhere" answer that was put together from the store and the preview.
The four answers to "does somebody else hold this path?" disagreed. A tab that
existed but had not yet been drawn counted as holding a document it had never
opened, so the analysis left its own copy open for good, and a preview asked
for the tokens of a document nobody had opened.

## Decision

- `Language/Server/DocumentHolders` records who holds each path: a tab, a
  preview, the analysis. The first holder opens the document with the server
  (after EnsureStarted), and the last to let go closes it. Nothing else opens
  or closes documents with the servers.
- The tab's text is the one the server keeps. A tab taking a document over
  reopens it with its text as a shown document, since a document's role is
  fixed when it is opened. A tab letting go while a preview or the analysis
  still holds the document reopens it with the text they last gave, as a
  consulted document. A preview or the analysis holding again refreshes the
  server's text only while no tab holds it.
- The three existing ports become the holders' front: the preview's and the
  analysis's Open and Close hold and let go, and the editor's open, change and
  close go through the holders as the tab.
- Every opener lets go of whatever it held, always. The preview no longer
  disowns a document a tab took over, and the analysis closes whatever it
  opened.
- A tab still opens its document when it is first drawn, not when it is
  restored, so a session with many tabs does not open them all at start-up.
  The analysis still skips a file a tab has open but not drawn, by asking the
  store as well as the holders.

## Rationale

The rule "close only when nobody holds it" is the whole safety property, and it
now lives in one small module with one test per transition, instead of being
reconstructed by three openers from flags that drifted apart.

## Consequences

### Positive

- A preview, a tab and the analysis can no longer take a document away from
  each other, and none of them can leave one open behind.
- The openers lost their disowning logic; "held elsewhere" for the analysis is
  one line.

### Negative

- A tab taking over a previewed or analysed document makes the server close and
  reopen it, so the server parses it once more.
- The holders remember the last text a preview or the analysis gave, a copy of
  the file for as long as they hold it.

### Neutral

- `PreviewSemantics::Holds` now answers for every file a pane shows.

## Alternatives Considered

- **Count references inside `LanguageServices`.** Lost, as 0596 found: the count
  would still need an owner deciding whose text wins, and the servers' code is
  the wrong place to know about tabs.
- **Keep the openers' flags and fix the two holes.** Lost: the next opener would
  need the same flags, and the answers had already drifted apart once.

## Related

- [0512](0512-name-one-owner-for-every-document-opened-with-a-language-server.md)
  deferred this mechanism.
- [0595](0595-keep-open-documents-in-a-store-shared-by-the-views-that-show-them.md)
  settled whose text wins.
- [0596](0596-send-didchange-when-a-document-s-text-moves-not-when-its-tab-draws-an-edit.md)
  rejected counting inside `LanguageServices`.
