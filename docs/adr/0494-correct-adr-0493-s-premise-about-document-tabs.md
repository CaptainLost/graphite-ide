# 0494. Correct ADR 0493's premise about document tabs

Status: proposed
Date: 2026-08-21

## Context

[0493](0493-draw-the-git-view-as-a-centrally-docked-panel.md) chose to draw
the git view as `Graphite::GitView : public Core::Panel`, docked into the
centre alongside the settings panel, rather than as a tab inside
`DocumentWindows`. Its Context section gave a reason for ruling out the tab
shape: "`DocumentWindows` is keyed by `std::filesystem::path`: every tab it
holds corresponds to a file that exists on disk... Nothing in
`DocumentWindows` today models a tab that isn't a document." Its rejected
alternative, reworking `DocumentWindows` around virtual documents, was framed
the same way — as a change to that keying, not attempted because it was a
larger decision than the work needed.

Building a diff as a document tab (`Graphite::DiffWindows`, alongside
`GitView`) showed the premise to be wrong. What is keyed by
`std::filesystem::path` is `Core::DocumentSet` — the collection of editable
open files — not the mechanism that makes something a tab. Reading
`DocumentWindows::RenderDocumentWindows` shows what actually produces a tab:
an `ImGui::Begin` window named `"<title>###<identity>"`, docked into the
centre node with `ImGui::DockBuilderDockWindow` the first time that identity
appears. Nothing about that mechanism requires the identity to be a
filesystem path or the window to correspond to a `Core::DocumentSet` entry —
`DiffWindows` produces exactly such a tab, keyed by a commit and a path
together, with no entry in `DocumentSet`, no `TextDocument`, and nothing
added to Core.

## Decision

0493's claim that a diff — or anything else not backed by
`Core::DocumentSet` — cannot be a document tab because `DocumentWindows` is
keyed by filesystem path is withdrawn. A tab is produced by an ImGui window
docked into the centre node; `Core::DocumentSet` is what happens to back the
tabs `DocumentWindows` currently draws, not a constraint the tab mechanism
itself imposes.

0493's decision is not superseded. `GitView` remains
`Core::Panel`, docked centrally beside the settings panel, and continues to
suit what it draws: a view keyed by selection state — ref, commit, file —
rather than by any single path, which is not what a tab identifies. That
ground was independent of the withdrawn one and still holds.

## Rationale

[0001](0001-record-architecture-decisions.md) asks a record to carry the
reasoning behind a decision, not only the decision itself, so that the
decision can be re-judged later against which parts of that reasoning still
hold. A reader who checks today whether a diff can be opened as a tab finds
that it can, `DiffWindows` sitting right beside `GitView` as proof — and
without this record, that reader has no way to tell whether 0493's premise
was wrong from the start or whether something changed underneath it since.
Editing 0493 in place would erase which reasoning it actually gave at the
time it was accepted, which is exactly the information a correction exists
to preserve. Writing the correction as its own record, in the shape
[0487](0487-correct-adr-0482s-premise-about-project-data.md) already set for
this repository, makes the correction a fact about the project's history
rather than something a reader has to infer from the gap between what 0493
predicted and what the code does now.

## Consequences

### Positive

- A reader of 0493 who follows its `Related` section to this record can tell
  precisely which part of the original reasoning was wrong, rather than
  guessing between "wrong" and "since changed".
- `DiffWindows` and any future tab that is not backed by `Core::DocumentSet`
  can point at `DocumentWindows::RenderDocumentWindows` as the actual
  contract for what makes a tab, instead of contending with a record that
  says the opposite.

### Negative

- 0493 now carries a withdrawn premise sitting alongside the reasoning that
  still holds, and a reader of 0493 alone, without following its `Related`
  section here, sees an unqualified claim about `DocumentWindows`'s keying
  that this record says did not hold even at the time it was written.
- This record changes no code; `GitView` was already a panel before it was
  written and remains one after. Its entire value is documentary.

### Neutral

- `Core::DocumentSet` is unaffected: it still holds exactly the open files a
  person can edit, keyed by path, and `DiffWindows` adds nothing to it. The
  correction is about what makes an ImGui window a tab, not about widening
  what `DocumentSet` tracks.
- Should a future document-shaped feature want to sit in `DocumentWindows`'s
  own tab strip rather than in a sibling structure such as `DiffWindows`,
  that remains the larger, not-yet-taken rework 0493's Alternatives
  Considered described — this record does not revisit whether to attempt it.

## Alternatives Considered

### Edit ADR 0493 in place

Remove the filesystem-path claim from its Context and Alternatives
Considered sections, leaving the rest of the decision as written. Rejected
for the same reason 0487 rejected editing 0482: a record documents what was
argued at the time it was accepted, and silently rewriting that argument
would erase the fact that this premise was once believed and later found
wrong, which is the one thing a reader most needs when re-judging a decision
like this.

### Supersede ADR 0493 with a new record

Write a full replacement that restates the panel decision without the
withdrawn premise. Rejected as disproportionate: 0493's decision — `GitView`
as a centrally docked panel — is unchanged and still well supported by its
other ground, so "superseded" would overstate what happened and would
discard 0493's Consequences and Alternatives Considered along with the one
sentence that needed correcting.

### Say nothing and let `DiffWindows` speak for itself

Let the existence of a diff tab demonstrate on its own that 0493's premise
did not hold. Rejected: a diff tab existing is consistent with either "the
premise was wrong" or "`DocumentWindows` was generalised since 0493 was
written", and only a record disambiguates which one a later reader is
looking at.

## Related

- [0493](0493-draw-the-git-view-as-a-centrally-docked-panel.md) — the record
  this one corrects one claim of, without superseding its decision.
- [0487](0487-correct-adr-0482s-premise-about-project-data.md) — the
  precedent this record follows for withdrawing a premise without
  superseding the decision it supported.
- `Graphite/src/Git/DiffWindows.h` and `.cpp` — the diff tabs whose existence
  is what shows 0493's premise did not hold.
- `Graphite/src/Editor/DocumentWindows.cpp` — `RenderDocumentWindows`, the
  mechanism that actually produces a tab: an ImGui window docked into the
  centre node, independent of `Core::DocumentSet`.
