# 0252. Per.17: Declare the most used member of a time-critical struct first

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Upstream gives this rule its title and nothing else — no reason, no example, no
enforcement. There is nothing to transcribe, so this record states the rule and
does not supply the argument for it.

## Decision

In a struct that has been identified as time-critical, declare the most used
member first.

- Leave every other type ordered for readability. The rule's own wording limits
  it to a time-critical struct.

## Rationale

Members declared in the same access-specifier group are laid out in declaration
order, so in a time-critical type the declaration order is a layout decision
whether or not anyone treats it as one.

## Consequences

### Positive

- The most used member sits at the start of the object, and the ordering of a
  hot type becomes a deliberate choice rather than an accident.

### Negative

- The record rests on a title alone. Upstream gives no reason, so the size of
  the effect claimed for this ordering cannot be checked against the guidelines.
- It fights readability directly: field order is one of the few structural cues
  a struct gives a reader, and this rule spends it — a cost
  [0015](0015-p-9-do-not-waste-time-or-space.md) already names.
- "Most used" is a property of an access pattern, not of the type, and a second
  caller with a different pattern makes the ordering wrong with nothing
  indicating it.
- Declaration order also determines padding, and ordering by use can enlarge the
  object. The rule does not mention the conflict.

### Neutral

- Declaration order fixes layout only within one access-specifier group. The
  relative order of members in different groups is unspecified.
- The rule gives no way to decide which struct is time-critical.

## Related

- [0251](0251-per-16-use-compact-data-structures.md) — the containing rule, and
  the one upstream does give a reason for.
- [0254](0254-per-19-access-memory-predictably.md) — member order only matters
  when the traversal order is already right.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — padding in a bulk-used type,
  and the readability it costs to remove.
