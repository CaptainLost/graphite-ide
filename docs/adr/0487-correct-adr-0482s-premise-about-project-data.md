# 0487. Correct ADR 0482's premise about project data

Status: proposed
Date: 2026-08-16

## Context

[0482](0482-keep-the-language-layer-in-graphite-rather-than-core.md) placed
`Lang` in Graphite, beside `Sln`, on two grounds. The one that stands is
[0002](0002-keep-core-free-of-ide-knowledge.md): Core does not get IDE
knowledge, and an LSP client — launching a server, speaking the protocol,
routing responses to panels — is exactly that. The other ground was a
prediction: 0482's Rationale argued that "a semantic analyser — the thing
that will eventually say what a symbol under the caret resolves to — needs
include paths, preprocessor definitions and a file list", parsed from
`.vcxproj` and `.csproj` by the readers `VcxprojReader`, `CsprojReader` and
`SolutionReader` that already live in `Graphite/src/Sln`, and it used that
shared need as a reason to put `Lang` next to `Sln` rather than in Core.

[0485](0485-take-code-intelligence-from-external-language-servers.md) settles
what actually answers "what does this symbol resolve to", and it is not an
analyser this repository writes. It is `clangd`, run as a subprocess. `clangd`
finds its own compile flags — from a `compile_commands.json` it discovers, or
from a best-effort guess when there is none — without this application
reading a `.vcxproj` and handing flags to it. `Lang`'s LSP client does not
call into `Sln`, does not parse a project file, and does not construct an
include path or a preprocessor definition anywhere in its path to `clangd`.
The premise 0482 used to justify its placement did not hold.

## Decision

0482's prediction about project data is withdrawn. No analyser is written in
this repository, and no compile flags are read by it; `clangd` locates its own
compile database, and `Lang` has no dependency, direct or planned, on `Sln`.

0482's decision is not superseded by this record. It stands on its remaining
ground: an LSP client is IDE knowledge, and [0002](0002-keep-core-free-of-ide-knowledge.md)
keeps IDE knowledge out of Core, which places `Lang` in Graphite regardless of
whether it ever shares data with `Sln`. This record narrows the argument for
where `Lang` lives; it does not move `Lang` or reopen where it lives.

## Rationale

An architecture decision record is a record of the reasoning that produced a
decision, not only the decision itself — 0001 asks for context that lets the
decision be re-judged later, and re-judging it means being able to see which
parts of the original reasoning still hold. Leaving 0482 as written would let
a reader who checks whether `Lang` calls into `Sln` today conclude, correctly,
that it does not, and then have no way to tell whether that is because the
prediction was wrong or because the shared need is still coming and simply
has not landed yet. Editing 0482 in place would erase which of its two
arguments did the work at the time it was accepted, which is the information
a later reader most needs when a decision like this one is questioned.
Writing the withdrawal down, as its own record, makes it a fact about the
project's history rather than something inferred from the absence of a
dependency that could just as easily mean "not yet" as "never".

## Consequences

### Positive

- A later reader of 0482 has a pointer to follow rather than a silent gap
  between what it predicted and what `Lang`'s dependencies turned out to be.
- `Lang`'s actual shape — an LSP client with no dependency on `Sln` — is no
  longer in tension with the record that placed it, because the tension is
  named and resolved here instead of left to be noticed.

### Negative

- 0482 now carries a withdrawn argument sitting alongside the one that still
  holds, and a reader of 0482 alone, without following its `Related` section
  to this record, sees an unqualified claim about `.vcxproj` data that this
  record says did not come to pass.
- This record exists only to say that a prediction was wrong; it changes no
  code and no dependency that was not already true after
  [0485](0485-take-code-intelligence-from-external-language-servers.md). Its
  entire value is documentary.

### Neutral

- `Sln`'s readers are unaffected. `VcxprojReader`, `CsprojReader` and
  `SolutionReader` keep doing exactly what they did before this record; the
  change is that `Lang` is not, and is not expected to become, one of their
  consumers.
- Should this repository later write something that does need `.vcxproj` data
  — a build-integration feature unrelated to code intelligence, say — that
  would be a new decision on its own grounds, not a reinstatement of the
  argument withdrawn here.

## Alternatives Considered

### Edit ADR 0482 in place

Update its Rationale to remove the `.vcxproj` argument and leave one ground
standing. Rejected: [0001](0001-record-architecture-decisions.md) treats a
record as a fact about what was decided and why at the time, not a living
description of the present; editing it would make the historical record say
something it did not actually argue when it was accepted, and would erase the
very information — that this ground existed and did not hold — that a
correction record exists to preserve.

### Supersede ADR 0482 with a new record

Write a full replacement record that restates the placement decision with
only the surviving argument. Rejected as disproportionate: 0482's decision —
`Lang` lives in Graphite — is unchanged, and "superseded" is for a record
whose decision no longer holds, not one that loses a supporting argument
while its conclusion stands on the other. Superseding it would also discard
0482's Consequences and Alternatives Considered, none of which this record
has any quarrel with.

### Say nothing and let the code speak

Rely on `Lang` simply not depending on `Sln` to demonstrate that the
prediction did not hold. Rejected per the Rationale: an absent dependency is
ambiguous between "the reason for it went away" and "the reason is still
pending", and only a record disambiguates which one a reader is looking at.

## Related

- [0482](0482-keep-the-language-layer-in-graphite-rather-than-core.md) — the
  record this one corrects one argument of, without superseding its decision.
- [0002](0002-keep-core-free-of-ide-knowledge.md) — the ground 0482's
  placement of `Lang` still stands on.
- [0485](0485-take-code-intelligence-from-external-language-servers.md) — the
  decision that made 0482's prediction moot: `clangd` finds its own compile
  flags.
- `Graphite/src/Sln` — the readers `Lang` does not, and is not expected to,
  depend on.
