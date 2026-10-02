# 0356. T.102: How to process arguments to a variadic template

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Upstream has not written this rule either. Its reason and enforcement sections
are placeholders, and where the example should be there are three words naming
the topics it would have covered: forwarding, type checking, references.

This record does no more than name them too. There is no upstream reasoning to
transcribe.

## Decision

Decide how a parameter pack is consumed, and say so in the template's
interface.

- Forward each element rather than copying it, and forward it once.
- State the requirement the pack's elements must satisfy — as a concept or a
  constraint over the pack — so that a bad argument is rejected at the call
  rather than at the first use inside the body.
- Be explicit about reference elements: decide whether the pack holds
  references or values, and do not let the answer depend on how the template
  happened to be called.

## Rationale

The three words upstream leaves are the three ways processing a pack goes
wrong: an argument copied when it should have moved, an argument accepted that
the body cannot use, and a reference whose lifetime nobody decided. Naming them
is the whole of what the rule offers.

## Consequences

### Positive

- The three failure modes are at least written down where a reviewer will see
  them.

### Negative

- This record rests on a title and a three-word fragment. It cannot be checked
  against upstream reasoning, because there is none, and it gives no technique
  for the actual work of expanding a pack.
- Stated at this level it overlaps almost entirely with the forwarding and
  concept rules, so it adds an entry to the index without adding guidance.

### Neutral

- The mechanics available — fold expressions, recursion, `std::apply` — are a
  C++20 language matter that upstream does not address in this rule.

## Related

- [0355](0355-t-101-how-to-pass-arguments-to-a-variadic-template.md) — the
  companion question, equally unwritten upstream.
- [0354](0354-t-100-use-variadic-templates-when-you-need-a-function-that-takes-a-variable-number-of-arguments-of-a-variety-of-types.md)
  — when a pack is the right tool.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — the type-checking half of this rule, stated properly.
