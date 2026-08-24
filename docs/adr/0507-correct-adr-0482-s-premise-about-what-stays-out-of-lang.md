# 0507. Correct ADR 0482's premise about what stays out of `Lang`

Status: proposed
Date: 2026-08-25

## Context

[0482](0482-keep-the-language-layer-in-graphite-rather-than-core.md) kept one
piece deliberately outside `Lang`: `SyntaxPalette`, which maps a `TokenKind`
to an `ImU32` and therefore includes `<imgui.h>`. Its Decision states the
reason as "`Lang` stays drawable-agnostic — nothing under it names a colour
or a draw call — specifically so that `GraphiteTests`, which links `Lang` and
not the editor widgets, can exercise the lexer and the cache without linking
ImGui." That sentence names two properties in one breath — "drawable-agnostic"
and "no ImGui" — and, having only the lexer and its cache to test the
boundary against at the time, never had to say whether they were the same
property or two. It also names a path, `Graphite/src/Editor/SyntaxPalette.h`,
that no longer exists: the type it describes now lives in
`Graphite/src/Editor/Palettes.h`, unrelated to this record's subject but worth
noting for a reader following the link.

This branch adds `Graphite/src/Lang/Diagnostics/DiagnosticsModel.h` — see
[0505](0505-hold-diagnostics-in-a-model-both-the-editor-and-the-panel-read.md).
`DiagnosticsModel` is neither a lexer nor a client: it is a UI-facing
model, purpose-built for a panel to read and a status bar to summarise, and
it is placed in `Lang/` anyway, on the ground that
[0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) already
established for `GitModel` — shared, refresh-driven state sits beside the
subsystem that produces it. `DiagnosticsModel` has no ImGui, no
`Core::TextBuffer`, and no tab width or drawing opinion of any kind; nothing
under it names a colour or a draw call, so it satisfies 0482's boundary on
its stated ground. But it is manifestly not "syntax" in the sense 0482's
Context was written about — it is a model built to be read by a panel, not a
step on the way to colouring a line — so its placement inside `Lang/` tests
whether 0482's boundary was ever "no ImGui" or was actually the narrower "the
lexer and its cache, and nothing beyond them." 0482's wording does not say
which.

## Decision

0482's boundary for what belongs in `Lang` is confirmed to be "no ImGui,"
not "syntax colouring only." `DiagnosticsModel` — a UI-facing model with no
lexer relationship at all — belongs in `Lang/Diagnostics` under exactly the
ground 0482 already gave for keeping `SyntaxPalette` out: it names no colour
and makes no draw call, so `GraphiteTests` can exercise it, like the lexer
and `SyntaxCache` before it, without linking ImGui. `Lang`'s boundary was
never "syntax," and this branch is what makes that explicit rather than
inferred from `Lang` having, until now, held nothing but syntax.

0482's decision is not superseded. `Lang` still lives in Graphite rather than
Core, still beside `Sln`, on the ground [0002](0002-keep-core-free-of-ide-knowledge.md)
gives and [0487](0487-correct-adr-0482s-premise-about-project-data.md) already
confirmed still holds. This record narrows what "drawable-agnostic" in 0482's
Decision means; it does not reopen where `Lang` sits or what may be placed
under it beyond settling that ImGui, specifically, is the line.

Separately, and not a decision: 0482 names `Graphite/src/Editor/SyntaxPalette.h`
as the file kept outside `Lang`. That path does not exist in this
repository; the type it describes now lives in `Graphite/src/Editor/Palettes.h`.
This record notes the discrepancy for a reader following the reference; it
does not correct 0482's text, per the same reasoning
[0487](0487-correct-adr-0482s-premise-about-project-data.md) and
[0494](0494-correct-adr-0493-s-premise-about-document-tabs.md) already give
for leaving a superseded record's original wording standing.

## Rationale

[0001](0001-record-architecture-decisions.md) asks a record to preserve the
reasoning that produced a decision so a later reader can re-judge it, which
means a reader checking today whether `Lang/Diagnostics` belongs where it
sits needs to know whether 0482 ever ruled it out. 0482's own sentence reads
either way: "drawable-agnostic" is a synonym for "no ImGui" if that is all it
meant, or a claim that `Lang` is about syntax specifically if that is what
"drawable" was standing in for. Nothing in 0482 needed to distinguish those
two readings before now, because the only thing ever placed under `Lang` was
the lexer and its cache — both readings agreed on every case 0482 had in
front of it. `DiagnosticsModel` is the first case where they diverge, so this
branch is what actually answers the question 0482 left implicit, and the
answer belongs in a record rather than being inferred a second time by the
next reader who has the same question.

Editing 0482 in place, or superseding it, would both overstate what
happened: no decision changed, and 0482's Decision text was not wrong, only
underspecified in a way this branch is the first thing to expose. This is the
same shape [0487](0487-correct-adr-0482s-premise-about-project-data.md) and
[0494](0494-correct-adr-0493-s-premise-about-document-tabs.md) already
established for this repository — a premise narrowed by what actually got
built, the decision it supported left standing — and this record follows it
rather than inventing a third way to write a correction.

## Consequences

### Positive

- A later reader deciding whether some new UI-facing-but-ImGui-free type
  belongs in `Lang` has an explicit answer — "no ImGui" is the test, not
  "is this syntax" — instead of having to infer one from precedent.
- `DiagnosticsModel`'s placement in `Lang/Diagnostics` is grounded in 0482's
  own stated boundary rather than sitting beside it as an unexplained
  exception a reader has to reconcile themselves.

### Negative

- 0482's Decision text still reads, on its own, as though `Lang` might mean
  "syntax colouring specifically" to a reader who does not follow this
  record's link — the ambiguity this record resolves is not visible from
  0482 alone.
- This record changes no code and no dependency; `DiagnosticsModel` was
  already placed in `Lang/Diagnostics` before this record was written. Its
  entire value, like 0487's and 0494's, is documentary.

### Neutral

- The stale path in 0482 — `Graphite/src/Editor/SyntaxPalette.h`, now
  `Graphite/src/Editor/Palettes.h` — is noted here but is not the substance
  of this record and does not, on its own, justify a correction; it is
  recorded because a reader following 0482's reference would otherwise hit a
  file that is not there.
- `Lang/Registry` and `Lang/Syntax`, the two families 0482 placed, are
  unaffected: this record clarifies the boundary they already sat inside,
  it does not move them or add a new one.

## Alternatives Considered

### Edit ADR 0482 in place to say "no ImGui" explicitly

Rejected for the reason [0487](0487-correct-adr-0482s-premise-about-project-data.md)
and [0494](0494-correct-adr-0493-s-premise-about-document-tabs.md) already
give: a record documents what was argued and understood at the time it was
accepted, and 0482's authors had only the lexer and its cache in front of
them, for which the two readings of "drawable-agnostic" were indistinguishable.
Rewriting it to sound as though it always meant "no ImGui" specifically would
erase the fact that the boundary was genuinely ambiguous until this branch
made it matter.

### Supersede ADR 0482 with a new record

Rejected as disproportionate, on the same grounds as 0487 and 0494: 0482's
decision — `Lang` lives in Graphite, beside `Sln` — is unchanged, and nothing
about `SyntaxPalette` staying out of `Lang` is wrong. Superseding it would
discard 0482's Consequences and Alternatives Considered over one sentence
that needed narrowing, not replacing.

### Say nothing and let `DiagnosticsModel`'s placement speak for itself

Rely on `Lang/Diagnostics` existing, with no ImGui dependency, to
demonstrate that 0482's boundary was always "no ImGui." Rejected per the
Rationale already given by [0487](0487-correct-adr-0482s-premise-about-project-data.md):
the absence of an ImGui dependency in `DiagnosticsModel` is consistent with
either "the boundary always meant this" or "the boundary was narrower and
was loosened without anyone deciding to," and only a record disambiguates
which a later reader is looking at.

## Related

- [0482](0482-keep-the-language-layer-in-graphite-rather-than-core.md) — the
  record this one corrects one premise of, without superseding its decision.
- [0487](0487-correct-adr-0482s-premise-about-project-data.md) and
  [0494](0494-correct-adr-0493-s-premise-about-document-tabs.md) — the
  precedent this record follows for narrowing a premise while leaving the
  decision it supported standing.
- [0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) — the
  placement precedent `DiagnosticsModel` follows: shared UI-facing state
  beside the subsystem that produces it.
- [0505](0505-hold-diagnostics-in-a-model-both-the-editor-and-the-panel-read.md)
  — the record that places `DiagnosticsModel` in `Lang/Diagnostics`, whose
  premise this record confirms.
- `Graphite/src/Lang/Diagnostics/DiagnosticsModel.h` — the type whose
  placement is the test case this record answers.
- `Graphite/src/Editor/Palettes.h` — where `SyntaxPalette` actually lives
  today, in place of the path 0482 names.
