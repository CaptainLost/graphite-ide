# 0231. ES.101: Use unsigned types for bit manipulation

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Unsigned types support bit manipulation without surprises from sign bits. Every
bit of an unsigned value is a value bit, so complement, shift and mask do what
the bit pattern suggests:

```cpp
unsigned char x = 0b1010'1010;
unsigned char y = ~x;   // y == 0b0101'0101;
```

Unsigned types can also be useful for modulo arithmetic. However, if you want
modulo arithmetic, add comments as necessary noting the reliance on wraparound
behaviour, as such code can be surprising for many programmers.

## Decision

Choose the signedness of a type by what you intend to do with the value.

- Use unsigned types for flags, masks, hashes, packed fields, checksums and
  anything else whose meaning is a bit pattern rather than a quantity.
- Use unsigned types where wraparound is the intended semantics, and say so in a
  comment at the point of reliance.
- Do not shift, complement or mask a signed value.
- Do not do arithmetic on a bit-manipulation value; if a bit pattern needs to be
  counted or compared as a number, convert it to a signed type at that point,
  and keep the two roles apart rather than letting one variable serve both.
- Fix the width when the bit pattern's width matters: prefer `std::uint32_t` and
  friends over `unsigned int`.

## Rationale

Signedness is not a property of the storage but a statement about what the bits
mean, and this rule and
[0232](0232-es-102-use-signed-types-for-arithmetic.md) are the two halves of one
decision: bit patterns are unsigned, quantities are signed. Where a value is a
bit pattern, the sign bit is not a sign, and a type that says otherwise invites
the compiler to sign-extend it on conversion or to treat a right shift as
arithmetic.

Marking the reliance on wraparound in a comment matters because the same
language behaviour is a defined feature here and a defect elsewhere; the comment
is the only way a reader can tell which was meant.

## Consequences

### Positive

- Complement, shift and mask behave as the bit-level reading of the code
  suggests.
- Right shift brings in zero bits. On a signed type it propagates the sign bit
  instead — an arithmetic shift, well defined since C++20 but not the
  bit-pattern operation the code appears to be asking for.
- Wraparound is defined behaviour, so the modulo idioms that need it are legal
  rather than merely working.

### Negative

- Bit-manipulation values are then unsigned, so any arithmetic performed on them
  either mixes signedness, which
  [0230](0230-es-100-dont-mix-signed-and-unsigned-arithmetic.md) forbids, or
  requires a conversion at every crossing.
- The same wraparound that makes modulo arithmetic legal makes accidental
  underflow silent, so unsigned values carry the risk described in
  [0234](0234-es-104-dont-underflow.md) with no diagnostic available.
- Narrow unsigned types are promoted to `int` in expressions, so `~x` on an
  `unsigned char` is computed as a signed `int` and then converted back; the
  rule holds for the result but the intermediate is not what it looks like.
- The comment requirement is unenforceable and is exactly the kind of comment
  that is not written.

### Neutral

- Enforcement is close to impossible in general, because the standard library's
  use of unsigned subscripts means unsigned values appear everywhere for reasons
  unrelated to bit manipulation.
- `std::bitset` and named flag enumerations avoid the question for many cases;
  this rule concerns raw integer bit twiddling.

## Enforcement

- Just about impossible in general, because of the use of unsigned subscripts in
  the standard library.

## Related

- [0232](0232-es-102-use-signed-types-for-arithmetic.md) — the other half of the
  same decision.
- [0230](0230-es-100-dont-mix-signed-and-unsigned-arithmetic.md) — what happens
  when a bit-pattern value is used in arithmetic anyway.
- [0236](0236-es-106-dont-try-to-avoid-negative-values-by-using-unsigned.md) —
  the illegitimate reason for choosing unsigned, as against this legitimate one.
- [0189](0189-es-26-dont-use-a-variable-for-two-unrelated-purposes.md) — why one
  variable should not be both a mask and a count.
