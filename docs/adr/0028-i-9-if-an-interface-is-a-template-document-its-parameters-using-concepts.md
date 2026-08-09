# 0028. I.9: If an interface is a template, document its parameters using concepts

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

There is no `concept` and no `requires` clause anywhere in Core or Graphite.
The templates that exist are few and narrow: `Result<T>`, `EventBus`'s
subscribe/publish pair, and a handful of container helpers.

## Decision

Adopt I.9 as written. Transcribed in full:

> **Reason.** Make the interface precisely specified and compile-time checkable
> in the (not so distant) future.

**Example.** Use the C++20 style of requirements specification. For example:

```cpp
template<typename Iter, typename Val>
  requires input_iterator<Iter> && equality_comparable_with<iter_value_t<Iter>, Val>
Iter find(Iter first, Iter last, Val v)
{
    // ...
}
```

See also: Generic programming and concepts.

**Enforcement.** Warn if any non-variadic template parameter is not constrained
by a concept (in its declaration or mentioned in a `requires` clause).

## Rationale

Adopted, and it is available: C++20 is the language floor by
[0005](0005-target-c-20-with-headers-not-modules.md), so `requires` and the
standard concepts are usable today without adding anything.

The rule is adopted for interfaces, which is what it says — a template that is
part of a header's public surface. It is not extended to implementation-detail
templates inside a `.cpp`, where the error message a constraint would improve is
one only the author will ever read.

`EventBus` is the case that would benefit most. Its `Subscribe<TEvent>` accepts
any type and erases it to `const void*`; a constraint saying what an event type
must satisfy would turn a confusing cast into a stated requirement.

## Consequences

### Positive

- Constrained templates fail at the call with a readable message instead of
  deep inside instantiation, which matters most for `EventBus`, whose errors
  currently surface inside a `std::function` conversion.
- The enforcement point is mechanical, so a future analyser could check it.

### Negative

- Every constraint is a name to invent and maintain. With this few templates the
  vocabulary would be mostly one-off concepts used once, which is overhead
  without the reuse that makes concepts pay.
- Adding constraints to `Result<T>` would restrict what it can hold for no
  present benefit; nothing has misused it.

### Neutral

- Nothing is retrofitted by this record. It applies to template interfaces
  written from here on.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| No template parameter anywhere is constrained by a concept or `requires` clause | `Core/src/Core/Base/Result.h`, `Core/src/Core/Event/EventBus.h` | `EventBus::Subscribe` is the one where a constraint would pay for itself |

## Related

- [0027](0027-i-8-prefer-ensures-for-expressing-postconditions.md) — the previous
  rule.
- [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md)
  — the next rule.
- [0005](0005-target-c-20-with-headers-not-modules.md) — why concepts are
  available.
