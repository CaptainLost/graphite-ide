# 0357. T.103: Don't use variadic templates for homogeneous argument lists

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

There are more precise ways of specifying a homogeneous sequence than a
parameter pack — `std::initializer_list<T>` being the obvious one. A pack
expresses "any number of arguments of any types"; when every argument is in
fact a `T`, that is strictly more freedom than the interface wants, and the
extra freedom is paid for in error messages, instantiations and overload
resolution.

Upstream states the reason in a sentence and leaves its example unwritten, so
the reasoning above is the whole of what it supplies.

## Decision

Use a sequence type, not a parameter pack, when all the arguments have the same
type.

- Take `std::initializer_list<T>` for a braced list of values written at the
  call site.
- Take a `span<T>` or a container reference where the caller already has the
  elements in storage.
- Reserve the parameter pack for genuinely heterogeneous argument lists, and
  for forwarding, where the pack's job is to preserve types it does not know.
- Where a pack is unavoidable but the elements must all be one type, constrain
  it so that the requirement is stated rather than discovered.

## Rationale

The type of the parameter is where an interface says what it accepts.
`f(std::initializer_list<int>)` says "some ints"; `template<class... Ts> f(Ts...)`
says nothing at all, and defers the whole question to instantiation. Choosing
the precise type also means the function is compiled once instead of once per
distinct argument list.

## Consequences

### Positive

- The signature states the requirement, so a wrong argument type is a call-site
  error with a short message.
- One instantiation serves every call, rather than one per argument list.
- The function stops being a greedy overload candidate for unrelated calls.

### Negative

- `std::initializer_list` gives const access to its elements and cannot be
  moved from, so a homogeneous list of move-only objects still needs a pack or
  a container.
- It also forces a common type: a call passing an `int` and a `long` compiles
  with a pack and may not with the list.
- A `span` requires the caller to have the elements contiguously in memory
  already, so the two replacements cover different situations and neither
  covers both.
- Upstream leaves the example unwritten, so the line between "homogeneous" and
  "heterogeneous enough for a pack" is left to judgement.

### Neutral

- The rule concerns the interface, not the implementation; a function taking an
  `initializer_list` may still be a template on the element type.
- Forwarding references remain the right tool for relaying arguments, whatever
  their types.

## Related

- [0354](0354-t-100-use-variadic-templates-when-you-need-a-function-that-takes-a-variable-number-of-arguments-of-a-variety-of-types.md)
  — the rule this one bounds.
- [0095](0095-c-103-give-a-container-an-initializer-list-constructor.md) — the
  same argument from the container's side.
- [0457](0457-f-24-use-a-span-t-or-a-span-p-t-to-designate-a-half-open-sequence.md)
  — the other precise way to say "a sequence of `T`".
