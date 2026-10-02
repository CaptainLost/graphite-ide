# 0097. C.109: If a resource handle has pointer semantics, provide * and ->

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Upstream supplies almost nothing for this rule. Its entire Reason is that
dereferencing is what is expected from pointers — familiarity — and its Example
and Enforcement parts are both empty. There is no code and no argument beyond
that one sentence to transcribe.

The only surrounding context upstream gives is the section's own definition: a
resource handle is a class that owns a resource, and `std::unique_ptr` and
`std::shared_ptr` are the archetypes of a handle whose value identifies an
object held elsewhere rather than being that object's contents.

## Decision

Where a resource handle has pointer semantics, give it the pointer operators.

- Provide `operator*` returning a reference to the owned object, and
  `operator->` returning a pointer to it.
- Do not provide `*` and `->` on a handle that does not have pointer semantics:
  a handle owning a file descriptor, a lock, or a window is not a pointer to
  anything, and should expose named operations instead.

## Rationale

Only the familiarity argument is available, and it is the rule's own. A reader
who meets a pointer-like type expects to dereference it, and a handle that
spells access as a named member cannot be substituted into generic code written
against the standard smart pointers without that code being rewritten by hand.

## Consequences

### Positive

- Access reads the same as it does for `unique_ptr`, so nothing has to be
  learned.
- The handle can be substituted into generic code written for smart pointers.

### Negative

- The record rests on a single sentence of upstream reasoning with no example,
  so there is nothing to check a fuller reading against. Questions the rule
  raises — whether a `const` handle yields access to a `const` object, what
  dereferencing an empty handle does — are left unanswered here because upstream
  does not answer them.
- Pointer syntax on an owning type invites the reader to treat it as
  non-owning, precisely where its ownership matters most.
- "Has pointer semantics" is not defined upstream, so the rule's own trigger is
  a judgement rather than a test.

### Neutral

- The rule does not say a resource handle should have pointer semantics, only
  what follows if it does.
- Upstream gives no enforcement text, which is why this record has no
  `Enforcement` section.

## Related

- [0093](0093-c-101-give-a-container-value-semantics.md) — the other half of the
  choice: value semantics rather than pointer semantics.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  why the resource is held in a handle at all.
- [0092](0092-c-100-follow-the-stl-when-defining-a-container.md) — the same
  argument from familiarity, applied to containers.
