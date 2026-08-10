# 0241. Per.4: Don't assume that complicated code is necessarily faster than simple code

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Simple code can be very fast, and optimisers sometimes do marvels with it. The
upstream pair of examples is the whole argument. The clear version:

```cpp
// clear expression of intent, fast execution
vector<uint8_t> v(100000);
for (auto& c : v)
    c = ~c;
```

And the clever version:

```cpp
// intended to be faster, but is often slower
vector<uint8_t> v(100000);
for (size_t i = 0; i < v.size(); i += sizeof(uint64_t)) {
    uint64_t& quad_word = *reinterpret_cast<uint64_t*>(&v[i]);
    quad_word = ~quad_word;
}
```

The second loop hand-vectorises eight bytes at a time, and is often slower. The
first loop states what it does in a form the optimiser recognises, and the
optimiser will widen it — very likely further than eight bytes at a time, using
whatever vector width the target actually has. The hand-written version has
fixed the width at compile time and taken that choice away.

It is also worse in ways that have nothing to do with speed. It reinterprets a
`uint8_t` sequence as `uint64_t`, which is an aliasing violation and gives
undefined behaviour; it assumes the buffer is suitably aligned; and it silently
does nothing to the tail if the size is not a multiple of eight. None of that is
visible in the code, and all of it is the programmer's problem for as long as
the code exists.

## Decision

Do not assume complexity buys speed; assume it costs clarity, and require
evidence for the speed.

- Write the loop that says what is meant — a range-`for`, or better a
  standard-library algorithm — and let the compiler transform it.
- Do not hand-vectorise, hand-unroll, or process a buffer through a wider type
  by casting. Reading or writing an object through a `reinterpret_cast` to an
  unrelated type violates the aliasing rules and gives undefined behaviour; it
  is not a fast path.
- If a hand-written version is proposed as faster, measure both, in a release
  build, with the optimiser on. A comparison made against a debug build measures
  nothing.
- When a complicated version does win, keep the simple version next to it as a
  comment or a test oracle, and record what the win was measured at.
- Remember that the simple version tracks the hardware: recompiled for a newer
  target it gets wider registers for free, while the hand-written one keeps the
  width chosen years earlier.

## Rationale

The optimiser is a better low-level programmer than a human on any code it can
understand, and complexity is precisely what stops it understanding. Every
hand-applied transformation is a constraint the compiler must now preserve,
including the ones that were never true improvements.

The undefined behaviour in the "fast" example is the sharpest part of it. The
clever loop is not a working program that happens to be slower; it is a program
whose behaviour the standard does not define, offered in exchange for a speed-up
that does not arrive.

## Consequences

### Positive

- Code stays readable, which is also the form the optimiser handles best.
- Whole classes of latent bugs — aliasing, alignment, unhandled tails — never
  enter the code.
- Simple code inherits improvements from every compiler upgrade and every wider
  target, without being touched.

### Negative

- It is not always true. There are real cases where hand-written code beats the
  optimiser, and this rule makes those cases harder to argue for.
- "Let the optimiser do it" depends on the optimiser: results vary between
  compilers and between optimisation levels, so the simple version's performance
  is less predictable than the explicit one, which matters where predictability
  is the requirement.
- Verifying that the compiler did vectorise a loop means reading generated
  assembly or optimisation reports, which is more specialised work than writing
  the intrinsic version would have been.

### Neutral

- The rule forbids assuming, not writing. A measured, documented, complicated
  version is permitted.
- Genuine SIMD work through intrinsics or a vector library is a different
  activity from casting a byte buffer to a wider type, and is not what the bad
  example shows.

## Related

- [0242](0242-per-5-dont-assume-that-low-level-code-is-necessarily-faster-than-high-level-code.md)
  — the same mistake stated at the level of abstraction rather than complexity.
- [0245](0245-per-10-rely-on-the-static-type-system.md) — the cast in the bad
  example is exactly the type violation that obstructs the optimiser.
- [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md) —
  what "is often slower" is grounded in.
- [0009](0009-p-3-express-intent.md) — the simple loop wins here for the same
  reason it wins there.
