# 0230. ES.100: Don't mix signed and unsigned arithmetic

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Mixing them yields wrong results. After the integral promotions, the usual
arithmetic conversions pick a common type by rank: where the unsigned operand's
type ranks at least as high as the signed operand's, and the signed type cannot
represent every value of the unsigned one, the signed operand is converted to
unsigned and a negative value becomes a very large positive one. That is the
case for `int` against `unsigned int`:

```cpp
int x = -3;
unsigned int y = 7;

cout << x - y << '\n';  // unsigned result, possibly 4294967286
cout << x + y << '\n';  // unsigned result: 4
cout << x * y << '\n';  // unsigned result, possibly 4294967275
```

Note that `x + y` prints `4`, which is the right answer; the conversion happened
there too and simply did not show. Where the signed type is instead wide enough
to hold every value of the unsigned one — `long long` against `unsigned int` on
a typical 64-bit platform — the conversion runs the other way and the mixture is
harmless. It is harder to spot the problem in more realistic examples, where the
two operands are named variables whose types, and therefore whose ranks, are
declared elsewhere.

Unfortunately, C++ uses signed integers for array subscripts and the standard
library uses unsigned integers for container subscripts. This precludes
consistency. Use `gsl::index` for subscripts.

## Decision

Keep the signedness of an arithmetic expression's operands the same.

- Choose signed types for arithmetic and keep them signed throughout the
  computation.
- Use `gsl::index` — a signed type wide enough for any subscript — for indices
  and sizes you compute with, rather than `size_t` or the container's
  `size_type`.
- Where a value must cross from an unsigned library type into signed
  arithmetic, convert it once, explicitly and narrowly, at the boundary, rather
  than letting the usual arithmetic conversions do it implicitly mid-expression.
- Do not silence a signed/unsigned warning with a cast without first deciding
  which signedness the computation should be performed in.
- Where a signed value must be compared against `.size()`, make it an index type
  of `ptrdiff_t` width rather than an arbitrary signed type; the guidelines'
  own enforcement exempts that shape from the warning, and
  [0237](0237-es-107-dont-use-unsigned-for-subscripts-prefer-gsl-index.md)
  explains why it is the least bad option. Convert explicitly in every other
  case.

## Rationale

The conversion is silent, it is applied by the language rather than requested by
the author, and it changes the value rather than merely the type. That is the
worst combination available: a well-formed expression with no diagnostic
produces an answer off by roughly four billion.

What makes the rule hard rather than obvious is that the standard library
forces the mixture on you — every `.size()` is unsigned and every built-in
subscript is signed. There is no way to be consistent with both, which is why
the guidelines resolve it by picking a side, signed, and naming a type for it.
This is [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md)
conceding a case: the type system permits the conversion, so the discipline has
to come from the choice of types.

## Consequences

### Positive

- Arithmetic gives the mathematically expected answer, or a diagnosable
  overflow, instead of a wrapped one.
- Compilers already know about the mixture and sometimes warn, so part of the
  rule is checkable by the build rather than by review.
- Subtraction becomes safe to write, which is where the mixture bites hardest.

### Negative

- The standard library is on the other side of this argument. Every
  `v.size()`, every `find` result and every `std::string` position is unsigned,
  so conforming means converting at a boundary that appears in almost every
  loop.
- `gsl::index` is a GSL type, so adopting it as the index type is a dependency
  decision, not just a style one; without the GSL a repository must pick its own
  signed alias and defend it.
- Conversions written at boundaries are themselves narrowing in the other
  direction — a `size()` above the signed maximum is not representable — so the
  rule moves the risk rather than eliminating it, even if it moves it somewhere
  unreachable in practice.
- Warnings must be left enabled and unsuppressed for this to be checked at all,
  and third-party headers frequently generate enough noise that projects turn
  them off.

### Neutral

- The mixture is a property of the expression, not of the variables, so the same
  variable may be fine in one expression and wrong in another.
- Which of signed or unsigned to standardise on is settled by
  [0232](0232-es-102-use-signed-types-for-arithmetic.md) and
  [0231](0231-es-101-use-unsigned-types-for-bit-manipulation.md); this record
  only says not to mix them.

## Enforcement

- Compilers already know and sometimes warn.
- To avoid noise, do not flag a mixed signed/unsigned comparison where one of
  the arguments is `sizeof` or a call to a container's `.size()` and the other
  is `ptrdiff_t`.

## Related

- [0232](0232-es-102-use-signed-types-for-arithmetic.md) — which side to pick
  when the two must be reconciled.
- [0231](0231-es-101-use-unsigned-types-for-bit-manipulation.md) — the
  legitimate use of unsigned types.
- [0237](0237-es-107-dont-use-unsigned-for-subscripts-prefer-gsl-index.md) — the
  subscript case, where the library forces the mixture.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — the
  static type safety the implicit conversion quietly costs.
