# 0236. ES.106: Don't try to avoid negative values by using unsigned

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Choosing `unsigned` implies many changes to the usual behaviour of integers,
including modulo arithmetic; it can suppress warnings related to overflow, and
it opens the door for errors related to signed/unsigned mixes. Above all, using
`unsigned` does not actually eliminate the possibility of negative values — it
only stops you seeing them.

```cpp
unsigned int u1 = -2;   // Valid: the value of u1 is 4294967294
int i1 = -2;
unsigned int u2 = i1;   // Valid: the value of u2 is 4294967294
int i2 = u2;            // Valid: the value of i2 is -2
```

These problems with perfectly legal constructs are hard to spot in real code and
are the source of many real-world errors. Consider:

```cpp
unsigned area(unsigned height, unsigned width) { return height*width; }
// ...
int height;
cin >> height;
auto a = area(height, 2);   // if the input is -2 a becomes 4294967292
```

Remember that `-1` assigned to an `unsigned int` becomes the largest
`unsigned int`. Also, since unsigned arithmetic is modulo arithmetic, the
multiplication did not overflow — it wrapped around.

```cpp
unsigned max = 100000;    // "accidental typo", I mean to say 10'000
unsigned short x = 100;
while (x < max) x += 100; // infinite loop
```

Had `x` been a signed `short`, we could have warned about the undefined
behaviour upon overflow. That last claim is slightly overstated as written:
`x += 100` on a `short` is computed in `int` and narrowed back, and the
narrowing conversion was never undefined — implementation-defined before C++20,
defined to wrap since — so what a compiler diagnoses there is the out-of-range
conversion. The point survives the correction: a signed type gives a diagnostic
something to fire on, and the arithmetic is genuinely undefined once it exceeds
`int`, whereas unsigned wraparound is a correct program by definition.

The alternatives the guidelines list are: use signed integers and check for
`x >= 0`; use a positive integer type; use an integer subrange type; or assert
`-1 < x`. For example:

```cpp
struct Positive {
    int val;
    Positive(int x) :val{x} { Assert(0 < x); }
    operator int() { return val; }
};

int f(Positive arg) { return arg; }

int r1 = f(2);
int r2 = f(-2);  // throws
```

## Decision

Do not use an unsigned type as a way of saying "this value cannot be negative".

- Declare quantities that happen to be non-negative as signed, and check
  `x >= 0` where the constraint matters.
- Where the constraint is part of the interface, express it with a precondition
  or with a type that establishes the invariant in its constructor, not with the
  signedness of a built-in type.
- Do not assume a parameter of unsigned type has excluded negative arguments;
  the conversion is implicit, silent and legal.
- Choose unsigned only for the reasons in
  [0231](0231-es-101-use-unsigned-types-for-bit-manipulation.md): bit
  manipulation and deliberate modulo arithmetic.

## Rationale

The argument that decides this is that `unsigned` is not a constraint. It is a
representation, and the conversion from a negative signed value is well-formed
and silent, so `area(height, 2)` with `height == -2` does not fail — it succeeds
with an answer four billion out. The type that looked like it enforced
non-negativity did nothing but destroy the evidence.

The infinite loop makes the second point. Because unsigned arithmetic is modulo
arithmetic, the wrap is the program working as specified, so a diagnostic has no
defect to point at; the same code with a signed type either narrows out of range
or, once the values exceed `int`, overflows into undefined behaviour, and both
are things a compiler or a sanitizer reports as a matter of course. Choosing
unsigned in the name of safety buys the one behaviour nothing is obliged to
complain about. This is
[0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) failing
by misuse of the type system rather than absence of it.

## Consequences

### Positive

- Negative values reaching a function that cannot accept them become detectable,
  by a check that can actually fire.
- Overflow in the computation stays diagnosable by a sanitizer instead of
  becoming defined wraparound.
- Expressing the constraint as a type or a precondition documents it where the
  signedness of a parameter never did.

### Negative

- The `Positive` type shown carries real costs: its implicit `operator int`
  is exactly the kind of conversion operator that
  [0129](0129-c-164-avoid-implicit-conversion-operators.md) warns against, and
  its constructor rejects zero, so the name is wrong for a length or a count.
- A validating type that reports failure by throwing, as `Positive` does,
  requires every construction site to be exception-safe and commits every caller
  to that failure mechanism — a large obligation to acquire from a decision
  about which integer type to use.
- Halving the positive range is a genuine loss for values close to the type's
  limit, and for some domains the unsigned range is the reason it was chosen.
- The rule pushes against the standard library, whose sizes, capacities and
  positions are unsigned for exactly the rejected reason.

### Neutral

- The guidelines' own note for this rule is an unfilled placeholder, so there is
  no additional guidance to transcribe.
- Whether the check belongs in a constructor, a precondition or a validating
  factory is a separate design decision.

## Enforcement

The enforcement for this rule is that of
[0230](0230-es-100-dont-mix-signed-and-unsigned-arithmetic.md): flag mixed
signed and unsigned arithmetic, and suppress the noise where one argument is
`sizeof` or a `.size()` call and the other is `ptrdiff_t`.

## Related

- [0232](0232-es-102-use-signed-types-for-arithmetic.md) — the positive form of
  this rule.
- [0230](0230-es-100-dont-mix-signed-and-unsigned-arithmetic.md) — the mixture
  this misuse of unsigned creates, and the source of its enforcement.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the
  constraint should be a type, not a representation.
- [0231](0231-es-101-use-unsigned-types-for-bit-manipulation.md) — the reasons
  for which unsigned is the right choice.
