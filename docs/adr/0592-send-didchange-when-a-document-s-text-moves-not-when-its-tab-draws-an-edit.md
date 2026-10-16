# 0592. Send didChange when a document's text moves, not when its tab draws an edit

Status: proposed
Date: 2026-10-16

## Context

didChange was sent from `UpdateLenses` when the tab drawn that frame reported `Edited`, plus a separate send on reload. An edit made by anything other than the drawing tab never reached the server. Colouring, semantic tokens and diagnostics overlays were kept per tab, keyed by path, inside `DocumentWindows`.

## Decision

- `DocumentStates` holds, per store document, the syntax cache, the semantic and diagnostics overlays, and the server bookkeeping (open or not, last change sequence sent).
- A document opens with its server once, when a view first shows it (today, a tab's first visible draw), with role `Shown`.
- Each frame, every open document whose change sequence moved since the last send gets one full-text didChange. The tab being drawn sends its own at once, so didChange still precedes that frame's symbol request.
- A document that leaves the store is closed with its server and forgotten.
- Reload no longer sends didChange itself for documents opened this way.

## Rationale

- The document, not the tab, is what the server holds.
- Driving didChange from the change sequence makes "who edited" irrelevant, which the results tab needs.

## Consequences

### Positive

- One send per change, from any view. Semantic tokens and diagnostics overlays are shared between views.

### Negative

- The per-frame sweep walks every open document.

### Neutral

- The tab width used for document-level overlays is the tab's. All tabs share the editor setting today.

## Related

- [0591](0591-keep-open-documents-in-a-store-shared-by-the-views-that-show-them.md)
- [0590](0590-keep-a-bounded-change-log-in-the-text-buffer-for-readers-to-catch-up-from.md)
- [0512](0512-name-one-owner-for-every-document-opened-with-a-language-server.md)
