# 0001. Record architecture decisions in this directory

Status: accepted
Date: 2026-08-09

## Context

This repository answers "why is it like this?" in three places, none of them
reliable. Feature specs under `docs/superpowers/specs/` describe what was being
built at the time and are not read again afterwards — and they are not even
version-controlled, being covered by `.gitignore`. Commit messages carry the
reasoning for a change but are found only by someone who already suspects where
to look. Coding conventions — that every control-flow body is braced, for one —
are written down nowhere at all.

The reader who suffers most from this is not a new team member; there is no
team. It is a coding agent starting a session with no memory of the last one,
which will reinvent a convention, or quietly reverse a decision, unless the
decision is written down somewhere it will actually look.

## Decision

Record every decision that outlives the feature that prompted it as a numbered
markdown file in `docs/adr/`.

**Write a record for:** choosing or dropping a dependency; a boundary between
modules; how errors cross an interface; a data format that lands on disk; a
rule that applies across the whole repository.

**Do not write one for:** a decision local to one file; a version bump; a bug
fix; a configuration change; anything a feature spec already covers in full.

Architecture and coding conventions share one directory and one numbering
sequence. Splitting them would mean deciding, for every record, which pile it
belongs in — and "return `Result<T>` rather than throwing" is genuinely both.

**Structure.** Filenames are `NNNN-kebab-case-title.md`; the heading is
`# NNNN. Title`. Numbers are four digits, assigned in ascending order and never
reused, so a number identifies a record permanently even after it is
superseded. Copy `template.md`, or run `.\scripts\New-Adr.ps1 "Title"`, which
allocates the number and appends the row to the index. Sections, in order:
`Context`, `Decision`, `Rationale`, `Consequences` split into `Positive`,
`Negative` and `Neutral`, then optionally `Alternatives Considered` and
`Related`. `Alternatives Considered` is required for architectural decisions and
optional for conventions.

`Consequences` must name a cost. An empty `Negative` heading means the decision
has not been thought through.

**Lifecycle.** `proposed` — written down, not yet in force. `accepted` — in
force; the normal state. `rejected` — considered and turned down; the record
stays, because the value of a rejected decision is that it stops the idea from
coming back. `superseded by NNNN` — replaced by a later record, which links
back. `deprecated` — no longer relevant, with no successor.

An accepted record is never rewritten on the merits. If the decision changes,
write a new record and mark the old one superseded. Fixing a typo is fine;
changing what a record says is not.

`README.md` is the index and is updated in the same commit as a new record or a
status change. `.\scripts\Test-Adr.ps1` checks that every record has the shape
described above and a row in the index.

## Rationale

A decision's reasoning is worth more than its outcome: the outcome is visible
in the code, the reasoning is not, and it is the reasoning that tells you
whether the decision still holds when circumstances change. Keeping superseded
records rather than editing them in place is what makes the directory show
movement instead of only the current state.

## Consequences

### Positive

- One place to look, for a human or an agent, before changing anything.
- A convention with a written reason is arguable; a convention in someone's
  head is only enforceable by that someone.
- Recording rejected options stops settled arguments from being reopened.

### Negative

- A record is a cost paid at the moment a decision is made, which is the moment
  the work feels most urgent. The threshold above exists to keep that cost from
  being paid on decisions that will not matter in a month.
- Records go stale. `Test-Adr.ps1` checks a record's shape, not its truth, and
  a confidently wrong record is worse than no record.
- Transcribed material — the C++ Core Guidelines records — forks from its
  upstream the moment it is written. Each such record carries a `Source:` line
  naming what it was copied from and when, so that the drift is at least
  detectable.

### Neutral

- Feature specs continue to live in `docs/superpowers/specs/`, ungoverned by
  this record. A spec says what is being built; a record extracts from it what
  must not be reversed without a conversation. A record may link to a spec; a
  spec is not edited to link back.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) and the records that follow it
  are the foundations this directory started with.
