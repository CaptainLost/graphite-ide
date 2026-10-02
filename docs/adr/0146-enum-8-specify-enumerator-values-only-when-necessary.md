# 0146. Enum.8: Specify enumerator values only when necessary

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Enum (Enumerations), transcribed 2026-08-10

## Context

It is the simplest thing to do. It avoids duplicate enumerator values. And the
default gives a consecutive set of values, which is good for switch-statement
implementations.

```cpp
enum class Col1 { red, yellow, blue };
enum class Col2 { red = 1, yellow = 2, blue = 2 }; // typo
enum class Month { jan = 1, feb, mar, apr, may, jun,
                   jul, august, sep, oct, nov, dec }; // starting with 1 is conventional
enum class Base_flag { dec = 1, oct = dec << 1, hex = dec << 2 }; // set of bits
```

`Col1` cannot have the defect `Col2` has: writing the values out created the
opportunity for two of them to be the same, and the compiler accepts it, because
duplicate enumerator values are legal and sometimes intended. The typo is
invisible until a `switch` quietly drops a case or a comparison matches the
wrong colour.

Specifying values is necessary to match conventional values, as in `Month`,
where the first enumerator is pinned to 1 and the rest follow by default; and
where consecutive values are undesirable, as in `Base_flag`, where each
enumerator must be a distinct bit and is written in terms of the one before it.

## Decision

Let the enumerator values default unless something requires otherwise.

- Write no values at all when the enumeration is a plain set of alternatives.
- Pin the first enumerator only, and let the rest follow, when the numbering has
  to start somewhere conventional.
- Write every value out when the values themselves are the point: bit flags,
  codes fixed by a specification, values that must match another system's.
- Write them out, and never renumber them afterwards, when the values are
  persisted or cross a process boundary; there the numbering is part of the
  format, not an implementation detail.
- Where an enumerator is deliberately an alias of another, write it as
  `alias = original` rather than repeating the literal, so that the duplication
  is visibly intentional.

## Rationale

The strongest argument is the one about typos: values that are not written
cannot be written wrongly, and the failure they prevent — two enumerators
sharing a value — is one the language will not diagnose, because the same
construct is a legitimate way to declare an alias. The default is not merely
tidier, it removes a class of silent bug.

Consecutive values from zero also happen to be the shape a compiler can turn a
`switch` into a jump table over, so the simplest thing to write is usually the
fastest thing to run; and the arguments that override this — a wire format, a
set of bits — are ones where the value has to be spelled out anyway, so they
announce themselves.

## Consequences

### Positive

- Duplicate values become impossible in the common case rather than merely
  unlikely.
- Adding an enumerator at the end costs one line and no arithmetic.
- Consecutive values from zero are what a compiler wants for a dense `switch`.

### Negative

- Defaulted values are positional, so inserting an enumerator in the middle
  silently renumbers everything after it. Where anything outside the program
  has seen those numbers — a saved file, a log, a stored setting — the change
  compiles cleanly and corrupts data, and the rule offers no signal about which
  enumerations are in that category.
- Flagging duplicate values has false positives: an intentional alias and a
  `count`-style sentinel are both duplicates, and both are legitimate.
- Flagging explicitly specified all-consecutive values fires on enumerations
  that pinned their numbering deliberately for ABI or protocol reasons, and on
  upstream's own `Product_info { red = 0, purple = 1, blue = 2 }` from
  [0139](0139-enum-1-prefer-enumerations-over-macros.md).
- Reading a defaulted enumeration in a debugger or a log shows an ordinal that
  has to be counted off against the declaration, since no value is written down
  anywhere.

### Neutral

- Partial specification is normal and is not a violation: `Month` pins `jan` and
  lets the rest follow.
- The rule says nothing about the underlying type, which
  [0145](0145-enum-7-specify-the-underlying-type-of-an-enumeration-only-when-necessary.md)
  handles on the same principle.

## Enforcement

- Flag duplicate enumerator values.
- Flag explicitly specified all-consecutive enumerator values.

## Related

- [0145](0145-enum-7-specify-the-underlying-type-of-an-enumeration-only-when-necessary.md)
  — the same principle applied to the underlying type.
- [0142](0142-enum-4-define-operations-on-enumerations-for-safe-and-simple-use.md)
  — bit-flag enumerations are the main case for writing values out, and they
  need operators too.
- [0140](0140-enum-2-use-enumerations-to-represent-sets-of-related-named-constants.md)
  — the `switch` exhaustiveness this rule's consecutive values support.
- [0009](0009-p-3-express-intent.md) — a value written out should mean the value
  itself matters.
