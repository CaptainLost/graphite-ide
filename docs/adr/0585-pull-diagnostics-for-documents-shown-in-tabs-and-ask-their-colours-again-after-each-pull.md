# 0585. Pull diagnostics for documents shown in tabs and ask their colours again after each pull

Status: proposed
Date: 2026-10-13

## Context

Graphite took diagnostics only from `textDocument/publishDiagnostics`. Measured on 2026-10-13
against a Unity project: Roslyn 5.13 publishes nothing and answers only
`textDocument/diagnostic`, so C# under Roslyn had no diagnostics at all; csharp-ls 0.28 stops
publishing once a client declares pull support; OmniSharp 2.0 offers no pulls. Roslyn also
answers `textDocument/semanticTokens/full` from frozen partial semantics: until something
builds the document's full compilation, names defined elsewhere in its solution come back as
`variable`, and a document not yet placed in a project comes back with no tokens. A
diagnostics pull builds that compilation; tokens asked right after its answer were complete.
The editor asked colours once when a server left `Starting`, which for Roslyn falls while it is
still loading its projects.

## Decision

Pull diagnostics from every server whose initialize result carries `diagnosticProvider`, for
documents opened `DocumentRole::Shown` - those in editor tabs, opened through the editor's
port - and never for `Consulted` ones (previews, the background pass, the warm-up). Declare
`textDocument.diagnostic` without dynamic registration and
`workspace.diagnostics.refreshSupport`.

A shown document is pulled when it opens after the handshake, when the handshake completes,
after any `didChange` of any document of that server (every shown document, whatever
`interFileDependencies` says), when the server sends `workspace/diagnostic/refresh`, and when
an adapter reports `ProjectsLoaded`; nothing is pulled while projects load. One pull is in
flight per document; a trigger meanwhile pulls once more after the answer. An error answer
publishes nothing and is retried once only when it asks to be retriggered. Closing a document
cancels its pull. An answer becomes an ordinary `PublishedDiagnostics` under the version the
pull was asked at, and the document is listed in `LspClient::Drained::Recolour`, which
`LanguageServices` and `DocumentServerPort` pass to `LspAnswerRouter`, which asks colours again
for each tab it follows.

## Rationale

Pulling is the only way Roslyn reports diagnostics, and its answer is the one deterministic
moment at which Roslyn's colours become complete, so one mechanism fixes both. Restricting it
to shown documents keeps the server from computing diagnostics nobody reads for every file the
background pass opens. Turning answers into publications leaves the model, the overlay, the
Problems panel and the stale dimming as they are.

## Consequences

### Positive

- C# under Roslyn and csharp-ls has squiggles, Problems entries and a status-bar count.
- Roslyn's colours are complete from the first pull on, including after a load that left them empty.
- clangd and OmniSharp, which offer no pulls, see no new traffic and recolour nothing.

### Negative

- Every edit pulls every shown document of the edited file's server; the cost grows with the
  number of open C# tabs, and csharp-ls computes them although it declares no inter-file
  dependencies.
- Each pull costs one more semantic-token request, cheap for servers whose colours were
  already complete.
- Colours of consulted documents are never asked again, so under Roslyn a preview can keep
  partial colours if asked before any shown document of its project was pulled.

### Neutral

- No `previousResultId` is sent, so every answer is a full report.
- `workspace/semanticTokens/refresh` is not declared: Roslyn sends it on every solution
  change, and the recolour after each pull covers what it would.

## Alternatives Considered

- Pull for every open document, previews and the background pass included. Loses: the server
  computes diagnostics for every file the pass reads, and nothing shows them.
- Honour `interFileDependencies`. Loses: under csharp-ls an error in one tab caused by an edit
  in another stays unseen until that tab is edited.
- Ask colours again on a timer, or after the lens's references answers. Loses: no deterministic
  moment; measured, a references answer did not make the next tokens complete.
- Declare `workspace/semanticTokens/refresh`. Loses: Roslyn sends it per keystroke, and it does
  not mean the compilation is complete.

## Related

- [0505](0505-hold-diagnostics-in-a-model-both-the-editor-and-the-panel-read.md): the model pulled answers reach.
- [0512](0512-name-one-owner-for-every-document-opened-with-a-language-server.md): the owners whose documents are Shown or Consulted.
- [0567](0567-keep-server-specific-behaviour-in-adapters.md): why the projects-loaded signal comes from the adapter.
- [0584](0584-read-a-publication-without-a-version-as-describing-the-version-last-sent.md): the publications this sits beside.
- The spec `2026-10-13-csharp-colours-and-pull-diagnostics-design.md`, which is not tracked.
