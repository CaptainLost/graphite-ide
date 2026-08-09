# 0016. P.10: Prefer immutable data to mutable data

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The shortest of the philosophy rules — upstream it is four sentences and a
cross-reference, with no example. It matters here mainly for the model types
that panels read while a background thread may be writing them.

## Decision

Adopt P.10 as written. Transcribed in full:

> **Reason.** It is easier to reason about constants than about variables.
> Something immutable cannot change unexpectedly. Sometimes immutability enables
> better optimization. You can't have a data race on a constant.
>
> See Con: Constants and immutability.

## Rationale

Adopted as written. Its last clause is the one that earns its place here: this
program has a watcher thread, so "you can't have a data race on a constant" is
not an abstraction — it is the reason `ChangeCollector` hands over a batch of
changes rather than exposing a mutable buffer.

The rule defers its substance to the Con section, so this record is deliberately
thin. The operative rules — `const` by default, `constexpr` where it applies —
are transcribed there, and this record exists so that the philosophy section is
complete and so that Con has something to point back to.

## Consequences

### Positive

- Gives the threading argument a written home: immutability is the cheapest
  synchronisation available.

### Negative

- Almost content-free on its own. A reader arriving here for guidance is sent
  elsewhere, and a record that only forwards is a record that will be skipped.
- `const` on members conflicts with move semantics and with assignability, so
  "prefer immutable" has a real cost in the model types that are stored in
  containers and reassigned.

### Neutral

- The substance is deferred to the Con section, which is not yet transcribed.
  Until it is, this rule has no operative detail in this directory.

## Deviations to fix

Not assessed. A meaningful inventory would mean auditing every member and
parameter for missing `const`, which belongs with the Con section rather than
here.

## Related

- [0015](0015-p-9-do-not-waste-time-or-space.md) — the previous rule.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — the next rule.
