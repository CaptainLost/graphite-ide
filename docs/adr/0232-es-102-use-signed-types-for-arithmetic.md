# 0232. ES.102: Use signed types for arithmetic

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Most arithmetic is assumed to be signed: `x - y` yields a negative number when
`y > x`, except in the rare cases where you really want modulo arithmetic.

Unsigned arithmetic can yield surprising results if you are not expecting it,
and this is even more true for mixed signed and unsigned arithmetic:

```cpp
template<typename T, typename T2>
T subtract(T x, T2 y)
{
    return x - y;
}

void test()
{
    int s = 5;
    unsigned int us = 5;
    cout << subtract(s, 7) << '\n';       // -2
    cout << subtract(us, 7u) << '\n';     // 4294967294
    cout << subtract(s, 7u) << '\n';      // -2
    cout << subtract(us, 7) << '\n';      // 4294967294
    cout << subtract(s, us + 2) << '\n';  // -2
    cout << subtract(us, s + 2) << '\n';  // 4294967294
}
```

Here everything has been made very explicit, but if you had seen `us - (s + 2)`
or `s += 2; ...; us - s`, would you reliably have suspected that the result
would print as `4294967294`?

Use unsigned types if you really want modulo arithmetic — add comments as
necessary noting the reliance on overflow behaviour, as such code is going to be
surprising for many programmers.

The standard library uses unsigned types for subscripts. The built-in array uses
signed types for subscripts. This makes surprises, and bugs, inevitable:

```cpp
int a[10];
for (int i = 0; i < 10; ++i) a[i] = i;

vector<int> v(10);
// compares signed to unsigned; some compilers warn, but we should not
for (gsl::index i = 0; i < v.size(); ++i) v[i] = i;

int a2[-2];         // error: negative size

// OK, but the number of ints (4294967294) is so large that we should get an exception
vector<int> v2(-2);
```

Note the asymmetry: the built-in array rejects the negative size at compile
time, while the vector accepts it and tries to allocate four billion `int`s.
Use `gsl::index` for subscripts.

## Decision

Perform arithmetic in signed types.

- Declare counters, sizes, offsets, differences and any other quantity you will
  do arithmetic on as a signed type; use `gsl::index` where the quantity is an
  index or a container size.
- Reserve unsigned types for bit manipulation and for deliberate modulo
  arithmetic, and comment the reliance on wraparound where you rely on it.
- Convert an unsigned library result, such as `.size()` or a `find` position, to
  the signed working type once, at the point it enters your computation.
- Do not choose an unsigned parameter type to express that a value must be
  non-negative; use a signed type and check.
- Prefer a type wide enough for the domain over a narrower unsigned one that
  merely doubles the positive range.

## Rationale

The decisive argument is in the rhetorical question the guidelines ask: nobody
inspecting `us - s` can tell from the expression whether the result will be `-2`
or `4294967294`, because the answer depends on declarations elsewhere. Signed
arithmetic removes the question entirely — the result of a subtraction is the
difference, always.

There is a second, less obvious benefit. Signed overflow is undefined behaviour
and unsigned overflow is defined wraparound, which sounds like an argument for
unsigned. It is the opposite: because signed overflow is undefined, sanitizers
and compilers are permitted to diagnose it, whereas wraparound is a correct
program producing a wrong answer and nothing may complain. See
[0233](0233-es-103-dont-overflow.md).

## Consequences

### Positive

- Subtraction behaves as subtraction, including when the result is negative.
- Overflow becomes detectable: `-ftrapv`, UBSan and equivalent tooling can flag
  signed overflow but cannot flag wraparound.
- Negative values arriving where they should not can be checked with `x >= 0`,
  which unsigned parameters make impossible.

### Negative

- The standard library is unsigned throughout, so this rule means converting at
  every interaction with a container size or position, and the conversions are
  themselves a place to get it wrong.
- Halving the positive range is a real loss for values that are genuinely
  non-negative and near the type's limit, such as file sizes and hashes on
  32-bit platforms.
- Signed overflow being undefined means the failure mode, where it happens, is
  worse than wraparound rather than better; the rule buys diagnosability at the
  price of undefined behaviour.
- `gsl::index` is a GSL type, so following the rule as written pulls in a
  dependency or requires a local equivalent.

### Neutral

- Modulo arithmetic remains a legitimate, explicitly permitted use of unsigned
  types.
- The rule says nothing about width; choosing a type large enough for the domain
  is a separate obligation, and [0233](0233-es-103-dont-overflow.md) covers it.

## Enforcement

- Flag mixed signed and unsigned arithmetic.
- Flag results of unsigned arithmetic assigned to or printed as signed.
- Flag negative literals, such as `-2`, used as container subscripts.
- To avoid noise, do not flag a mixed signed/unsigned comparison where one of
  the arguments is `sizeof` or a call to a container's `.size()` and the other
  is `ptrdiff_t`.

## Related

- [0230](0230-es-100-dont-mix-signed-and-unsigned-arithmetic.md) — the rule this
  one resolves, by saying which side to standardise on.
- [0231](0231-es-101-use-unsigned-types-for-bit-manipulation.md) — the
  complementary rule for bit patterns.
- [0236](0236-es-106-dont-try-to-avoid-negative-values-by-using-unsigned.md) —
  the tempting misuse of unsigned that this rule rules out.
- [0237](0237-es-107-dont-use-unsigned-for-subscripts-prefer-gsl-index.md) — the
  subscript case in detail.
