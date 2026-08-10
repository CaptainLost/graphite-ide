# 0355. T.101: How to pass arguments to a variadic template

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Upstream has not written this rule. Its title is itself a question marked as
unfinished, and its reason and enforcement sections are placeholders. The only
substantive fragment in the whole entry stands where the example should be:
beware of move-only and reference arguments.

There is therefore nothing to transcribe beyond that warning, and this record
does not pretend otherwise. The passing convention itself is settled elsewhere
in the guidelines, by the rule on forwarding parameters.

## Decision

Decide deliberately how arguments reach a variadic template, and treat
move-only and reference arguments as the cases that will expose a careless
choice.

- Take a forwarding pack, `Ts&&... args`, and pass it on with
  `std::forward<Ts>(args)...` exactly once, as the forwarding rule requires.
- Do not take the pack by value in a template that only relays it; a move-only
  argument then has to be moved into the pack and moved again out of it.
- Be explicit about whether a reference argument is meant to be bound or
  decayed, and use `std::ref` at the call site where a reference must survive
  the pack.

## Rationale

Move-only and reference arguments are named upstream because they are the two
cases where a pack's passing convention is observable: one fails to compile,
and the other silently copies or dangles. Everything else about passing a pack
follows from the ordinary parameter-passing rules.

## Consequences

### Positive

- The one hazard upstream identified is written down rather than discovered.
- The convention is inherited from an existing rule instead of being invented
  here.

### Negative

- This record rests on a title and a one-line warning. Upstream supplies no
  reason, no example and no enforcement, so nothing in it can be checked
  against the guidelines' reasoning.
- Because it is a question rather than a rule, it cannot be complied with or
  violated, and it largely duplicates the forwarding rule that does have
  content.

### Neutral

- What happens to the arguments once inside the template is a separate rule,
  equally unwritten upstream.

## Related

- [0451](0451-f-19-for-forward-parameters-pass-by-tp-and-only-std-forward-the-parameter.md)
  — the rule that actually settles how a forwarded argument is passed.
- [0354](0354-t-100-use-variadic-templates-when-you-need-a-function-that-takes-a-variable-number-of-arguments-of-a-variety-of-types.md)
  — when to have a pack at all.
- [0356](0356-t-102-how-to-process-arguments-to-a-variadic-template.md) — the
  companion question, about the other end of the same pack.
