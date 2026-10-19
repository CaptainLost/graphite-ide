# 0622. Keep document symbols' ranges and nesting and ask for them again after each save

Status: proposed
Date: 2026-10-19

## Context

- A `Declaration` said where a function or type begins and where its name
  begins, nothing more. `CollectSymbols` flattened a hierarchical
  `documentSymbol` answer, so a method and its class arrived as two unrelated
  entries. A breadcrumb that names the innermost function or type at the caret,
  with its chain (`Class › Method`), needs each declaration's last line and its
  parent.
- Symbols were asked when a document opened, reloaded or its server changed, and
  after every didChange ([0596](0596-send-didchange-when-a-document-s-text-moves-not-when-its-tab-draws-an-edit.md)).
  A want made while a question was outstanding was lost: the older question's
  answer cleared it, so the last edits of a burst were never answered for.
- Between answers the lens rows moved with edits (`LensModel::OnLinesChanged`),
  but `LensState::Declarations` did not. A wrap-width rebuild put lenses back on
  stale lines, a lens click searched stale lines, and a closing tab wrote stale
  lines to the store.
- Every symbols answer cleared the lens "asked" set, so every visible lens whose
  count was in flight or had failed was asked again. Counts were carried from one
  answer to the next by name and kind alone, so every overload took the first
  overload's count.
- The analysis record ([0513](0513-keep-the-workspace-analysis-on-disk-and-discard-it-on-any-doubt.md))
  holds declarations, and a change to its format must throw old records away.

## Decision

- **Ranges and nesting.** `Declaration` gains `EndLine`, the last line of the
  symbol's `range` (not its `selectionRange`), and `Parent`, the index of its
  parent in the same vector or `kNoParentDeclaration` (−1). A range with no end
  ends on its first line.
- **Hierarchy.** `CollectSymbols` keeps a `DocumentSymbol[]` answer in
  pre-order: each declaration comes before its children, and each child names
  its parent. A symbol this editor does not show, such as a namespace, passes its
  own parent on to its children. A flat `SymbolInformation[]` answer takes
  `EndLine` from `location.range` and names no parent. Nesting is never inferred
  from ranges or from `containerName`.
- **The chain at a line.** `InnermostDeclarationAt(declarations, line)`, in
  `Language/Api/Declarations.h`, returns the declarations that contain the line,
  outermost first. It finds the innermost one and walks its parents. A
  declaration's first and last lines are inside it. A line no declaration
  contains gives an empty chain.
- **Edits move declarations.** Every edit the editor reports to the lens model
  also moves the document's declarations (`MoveDeclarations`), from the same
  first moved line and by the same line delta.
  - A line at or after the first moved line shifts, so a line inserted inside a
    range extends its end.
  - A declaration whose first line is deleted is dropped, as its lens is, and the
    declarations it held take its parent.
  - A range whose last line is deleted ends on the line before the deletion.
- **A save asks again.** Every successful save goes through
  `DocumentStates::NoteSaved`, which sends didSave and wants symbols at once.
  That covers Save, Save All, and saves made from the close and overwrite
  questions. Save All now sends didSave as well, and `Core::SaveAllReport::Saved`
  lists the saved paths. A failed save asks nothing.
- **A want outlives the question in flight.** A want made while a symbols
  question is outstanding keeps symbols wanted after that question's answer, so
  one more question is asked about the newer text.
- **"Asked" belongs to the lens.** Whether a lens's count has been asked is kept
  on its entry in `LensModel` (`TakeRowsToAsk`, `ForgetAsked`), not in a set of
  rows beside it. A symbols answer does not reset it. An edit, a server change
  and a rebuild forget it, as before.
- **Matching a lens to a new answer.** A new declaration takes the count of the
  previous lens with the same name and kind on the same row. Failing that, it
  takes the first unmatched one with that name and kind, in row order. Failing
  that, it takes a count restored from the store, by name and kind, in order.
  Only a same-row match keeps "asked", because a count in flight answers to the
  row it was asked for. After a save, then, lenses keep their counts, a count in
  flight is not asked again, and only declarations new since the last answer are
  asked, through the visible-rows path.
- **Record version 2.** The analysis record stores `EndLine` and `Parent` with
  each declaration, and `kAnalysisRecordVersion` is 2. A version 1 record or
  index is discarded, as 0513 discards any record a version behind. There is no
  migration. A record naming a parent that is not an earlier declaration is
  refused.

## Rationale

The server already states each symbol's extent and nesting, so keeping them
costs two fields. Moving declarations on the notification that moves lenses keeps
the two in step between answers. Keeping "asked" on the lens lets an answer
replace the declarations without forgetting which questions are still out.

## Consequences

### Positive

- The breadcrumb can show `Class › Method` from data already held, with no
  question per caret move.
- A function typed and saved shows once the save's answer arrives, even when a
  question was out at the time of the save.
- Overloads keep their own counts. A wrap change no longer puts lenses back on
  stale lines, and a lens click finds its declaration after an edit.
- A symbols answer no longer asks again about every visible lens whose count is
  in flight.

### Negative

- Every store written by an earlier build is thrown away once, and its files are
  analysed again.
- A save asks for symbols even when the question in flight already describes the
  saved text: one extra question per save. While it is out, the document counts
  as analysing, so the status bar names it and the background pass yields.
- A failed count is asked again only after an edit, a server change or a
  rebuild, no longer after the next symbols answer.
- Declarations move only with edits made in an editor view, as lenses do. An
  edit made elsewhere leaves them where they were until the next answer.

### Neutral

- A flat answer gives a chain of one declaration.
- `MoveDeclarations` follows the lens model's line arithmetic, so a multi-caret
  edit moves everything after its first moved line by its total delta.

## Alternatives Considered

### Infer nesting from ranges

Containment would give a chain for flat answers too. Rejected: hierarchical
answers already state their nesting, the client advertises hierarchical support,
and for a flat answer containment guesses at a nesting the server did not state.

### Keep "asked" as a set of rows and stop clearing it on an answer

The smallest change. Rejected: a new declaration landing on a row asked for an
old one would never be asked, and the set lived in `DocumentWindows`, which no
test can drive.

### Let the lens model own the declarations

One edit hook would then move both. Rejected for now: a restored analysis is
anchored a frame late, and moving ownership would touch every reader of
`LensState::Declarations` with no behaviour gained.

### Ask after a save only when the text moved since the last answer

Saves the extra question. Rejected: it needs the change sequence each answer
describes, to save one question per save.

## Related

- [0513](0513-keep-the-workspace-analysis-on-disk-and-discard-it-on-any-doubt.md) —
  the record format this bumps; amended here.
- [0596](0596-send-didchange-when-a-document-s-text-moves-not-when-its-tab-draws-an-edit.md) —
  `DocumentStates` as the document's line to its server; a save now goes through
  it too. Amended here.
- [0486](0486-insert-non-document-rows-above-rowmap-rather-than-inside-it.md) —
  the lens rows whose counts are kept.
- `Graphite/src/Language/Api/Declarations.h` — `InnermostDeclarationAt` and
  `MoveDeclarations`.
