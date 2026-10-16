# 0595. Keep open documents in a store shared by the views that show them

Status: proposed
Date: 2026-10-16

## Context

A tab held its document by value (`OpenItem::Document`), so a file could be shown and edited by exactly one view. Two views would have meant two texts, two undo histories, two dirty flags and two didOpens. [0512](0512-name-one-owner-for-every-document-opened-with-a-language-server.md) deferred reference-counting the server open for exactly that reason ("whose text wins").

## Decision

- `Core::DocumentStore`, owned by `DocumentSet`, maps a path (exact match) to a `std::weak_ptr<TextDocument>`. Views hold `std::shared_ptr`s, so a document lives while any view holds it and dies with the last.
- `Acquire` returns the registered document or loads it.
- Disk questions that are about text, not tabs, ask the store: Save All, the exit prompt's list, "is this file open", `BufferOfOpenDocument` and change-marker retention.
- Missing/present marking and load failures stay on the tab.
- Only the last holder asks about unsaved changes before letting go.

## Rationale

- Shared ownership is the real relationship.
- A weak-reference store needs no release protocol, and no view can forget to release.
- Owning the store in `DocumentSet` keeps every existing constructor.

## Consequences

### Positive

- One text per file, whatever shows it. 0512's question has an answer.

### Negative

- A document's lifetime is no longer visible in one place. Tests and code must not keep stray `shared_ptr` copies, which would keep a closed file alive.

### Neutral

- The store keeps registration order, which is tab order today.

## Alternatives Considered

- Views register and release explicitly with a reference count in the store: every view must remember to release, and a forgotten release leaks the document.
- The store owns the documents outright: it needs a release protocol and a rule for when an unreferenced document may be dropped, which weak references answer by themselves.

## Related

- [0512](0512-name-one-owner-for-every-document-opened-with-a-language-server.md)
- [0594](0594-keep-a-bounded-change-log-in-the-text-buffer-for-readers-to-catch-up-from.md)
