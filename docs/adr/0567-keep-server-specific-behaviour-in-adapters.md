# 0567. Keep server-specific behaviour in adapters

Status: proposed
Date: 2026-10-09

## Context

[0485](0485-take-code-intelligence-from-external-language-servers.md) promised that a further language costs one table row. The row (`ServerConfig{Command, ProtocolLanguageId, RootMarkers}`) could not carry a second server, and clangd's habits had leaked into the generic client: its launch flags went to every server, every client reported "no compile database", hover replies went through the clangd parser, the warm-up opened a document for every server, and every unknown server request was refused. C# servers need more still: Roslyn wants a `solution/open` notification after `initialized` and answers to requests of its own.

## Decision

A server is a row in `ServerCatalog` — id, display name, executable, protocol language id, default arguments, root markers, adapter kind. What a row cannot say lives in a `ServerAdapter` subclass the row names: initialization options, answers to `workspace/configuration`, notifications after `initialized`, answers to server-specific requests, the project problem it reports, whether the warm-up must open a document, whether it can switch between header and source. `LspClient` and `LanguageServices` never test which server they are talking to; they ask its adapter. The base `ServerAdapter` is plain LSP.

## Rationale

The second server then costs a row and, only if it misbehaves, a class — and one server's quirks are tested without going through another's code path.

## Consequences

### Positive

clangd behaves as before with its quirks in one file; a test drives any hook through a fake adapter.

### Negative

A server's behaviour is split between its row and its adapter class; an adapter with many hooks can grow into a second client, so a new hook needs a server that needs it.

### Neutral

Hover parsing still assumes clangd's markdown; it moves into the adapter with the first server that answers hover differently.

## Alternatives Considered

- Branches on the server's identity in the client (`if (server == clangd)`) — loses because every server's quirk would run through every other server's code and tests.
- Data-only definitions with no code — loses because a notification whose payload is computed at run time (the solution Roslyn should open) cannot be written as data.
- Adapters loaded as plugins — loses because the languages are built into Graphite (spec decision).

## Related

- [0485](0485-take-code-intelligence-from-external-language-servers.md): the decision that a language costs one table row.
- [0488](0488-ship-clangd-with-the-release.md): clangd as the shipped server.
- [0566](0566-store-nested-values-in-graphite-json.md): where the per-server choices and options are stored.
- The spec `2026-10-09-language-servers-design.md`, which is not tracked.
