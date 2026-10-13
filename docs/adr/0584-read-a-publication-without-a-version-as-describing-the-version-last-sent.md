# 0584. Read a publication without a version as describing the version last sent

Status: proposed
Date: 2026-10-13

## Context

[0506](0506-dim-a-diagnostic-the-buffer-has-outrun-rather-than-dropping-it.md) dims a
diagnostic whose publication names an older version than the one last sent, and reads a
publication that names no version as `DocumentVersion{}`, which is stale as soon as anything
has been sent. It was written when clangd, which always names the version, was the only
server. Two of the three C# servers do not: OmniSharp 2.0.0 publishes `"version": 0`, and
csharp-ls 0.28.0, while it publishes, sends no version at all. Graphite numbers versions from
1, so every one of their diagnostics read as stale and was dimmed for as long as it stood.

## Decision

`LspClient::PublishDiagnostics` stamps a publication whose version is absent or 0 with the
version this client last sent for that document, before it leaves the client. This replaces
0506's reading of a publication without a version as stale; the rest of 0506 stands.

## Rationale

A publication with no version gives nothing to compare, and the only text it can describe is
the text the server has, which is the last version sent. Dimming it forever hides every
diagnostic of two servers to guard against a lag nobody can measure.

## Consequences

### Positive

- OmniSharp's diagnostics, and csharp-ls's when it publishes, draw at full strength.
- `DiagnosticsModel`, the overlay and the Problems panel are unchanged; the stamp happens where
  the document's version is known.

### Negative

- A publication a server computed for older text and sent without a version now reads as
  current until the next one replaces it. Such a server gives nothing to tell the two apart.

### Neutral

- `DiagnosticsModel::Entry::HasAnswer` still separates a document never answered from one
  answered; `DocumentVersion{}` now only ever means "never sent" for anything a client delivers.

## Alternatives Considered

- Keep 0506's reading. Loses: every OmniSharp diagnostic is dimmed, permanently.
- Stamp only for servers an adapter marks as unversioned. Loses: the same behaviour behind a
  flag that each new server would need setting, for no case where the plain rule is wrong.
- Drop publications without a version. Loses: the diagnostics themselves.

## Related

- [0505](0505-hold-diagnostics-in-a-model-both-the-editor-and-the-panel-read.md): the model these publications reach.
- [0506](0506-dim-a-diagnostic-the-buffer-has-outrun-rather-than-dropping-it.md): the clause this replaces.
