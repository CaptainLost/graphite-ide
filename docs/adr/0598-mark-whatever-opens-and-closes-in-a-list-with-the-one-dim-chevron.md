# 0598. Mark whatever opens and closes in a list with the one dim chevron

Status: proposed
Date: 2026-10-16

## Context

The trees in the Explorer and the solution view, and the groups of Find in Files results, opened under ImGui's own tree arrows. Those are bright triangles in the text colour. Changes, the branch list and the review opened under thin chevrons in the disabled-text colour. The gesture is the same in every place, but it looked like two different things.

ImGui draws its arrow in the text colour and has no flag to leave it out.

## Decision

Mark whatever opens and closes in a list or a tree with one chevron in the disabled-text colour: `ChevronGlyph`, `DrawChevron` and `ChevronTreeNode` (`Graphite/src/UI/Lists/Chevron.h`).

- `ChevronTreeNode` is used as `ImGui::TreeNodeEx` is: `SetNextItemOpen` before it, and `TreePop` after it when it is open.
- A plain node is a list row (see [0599](0599-draw-every-list-row-through-listrowselectable.md)). It keeps its open state where ImGui keeps a tree node's, under the same id.
- A framed node, a collapsing header, is ImGui's own, with its arrow cleared and the chevron drawn in its place.
- Both draw the label where, and in the colour, ImGui would draw it. An optional note, such as a count or a cost, follows the label in the disabled-text colour.

## Rationale

**Quiet marks.** A chevron in the disabled-text colour does not compete with the names beside it. Most of the newer panels already looked this way.

## Consequences

### Positive

- Every list and tree opens under the same mark.

### Negative

- A plain node no longer opens and closes with the Left and Right arrow keys. Enter or Space on a focused row still toggles it.
- The framed path mirrors ImGui's own placement of the arrow and the label, so it can drift if ImGui changes it.

### Neutral

- Chevrons in the history's commit rows were bright before; they are dim now, like every other chevron.

## Alternatives Considered

- **Triangles everywhere.** Easy to spot, but heavy in dense lists such as the branches and the changes.
- **Chevrons in the text colour.** In long lists they blend into the text.

## Related

- [0599](0599-draw-every-list-row-through-listrowselectable.md): the list row a plain tree node is.
