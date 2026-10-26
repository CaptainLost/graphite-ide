# 0641. Let the view on screen add its own items to the status bar

Status: proposed
Date: 2026-10-26

## Context

The right-hand end of the status bar holds the problem counts, the server's
words and the background pass, and then, only when a text document is shown, its
caret position, encoding and line ending
([0620](0620-show-problems-and-the-server-s-health-as-glyphs-and-colours-in-the-status-bar.md)).
A tab that is not a text document, such as a picture, has nothing to put there,
yet it has facts of its own worth the bar: a picture's size, format and file
size, and the colour under the pointer.

The bar draws from `StatusMessages`, data `GraphiteLayer` builds each frame. It
knows nothing of the views, and should not learn their kinds.

## Decision

- **The data.** `StatusMessages::View` is a vector of `ViewStatusItem`, each a
  `Text`, a `Code` flag and an optional `Swatch` colour. The view on screen
  fills it; the bar draws it and nothing more.
- **Where they stand.** When a text document is shown, `View` is ignored and the
  caret, encoding and line ending stand as before. With no document shown, one
  item per entry takes their place, after the problems, the server and the
  background pass, so the order and the gaps are the bar's own.
- **The face.** An item is in the interface face at the bar's size, and in the
  code face at the bar's size when `Code` is set, as the caret position is.
- **The swatch.** An item with a `Swatch` draws a square 11 px across, scaled,
  centred on the text line, before its text and 4 px from it, as the server's
  dot is. The square is a 2x2 checkerboard with the colour drawn over it, so a
  translucent colour shows as one.
- **Inert.** A view's item takes no click and no tooltip.

## Rationale

Handing the bar finished items keeps it free of every view's kind, as 0620 keeps
it free of the language services. Showing them only where the caret, encoding
and line ending would stand gives a view the spot a document has, and leaves a
document's items alone. A checkerboard under the swatch is how a colour with
alpha is told from one without.

## Consequences

### Positive

- A view that is not text puts its facts in the bar without the bar knowing it.
- A document's items are untouched.

### Negative

- A view's items cannot be pressed or explained by a tooltip until the data
  says how.
- The checkerboard's two greys are the same on every scheme.

### Neutral

- Nothing yet fills `View`; the layer that builds `StatusMessages` does.

## Alternatives Considered

- **Let each view draw into the bar itself.** It would allow any item, but every
  view would need the bar's fonts, gaps and hover rules, and the bar's order
  would no longer be its own.
- **Give the bar a picture's size, format and colour directly.** It would need
  no new shape, but the bar would learn each view's kind, the coupling 0620
  refuses.

## Related

- [0620](0620-show-problems-and-the-server-s-health-as-glyphs-and-colours-in-the-status-bar.md):
  the bar's items, gaps and the dot this swatch follows.
