# 0015. P.9: Do not waste time or space

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

The upstream reason for this rule is three words long: "This is C++." A language
chosen for control over time and space is being used badly when that control is
spent on nothing.

Time and space spent well to achieve a goal — speed of development, resource
safety, simplification of testing — is not wasted. As Alex Stepanov put it,
"another benefit of striving for efficiency is that the process forces you to
understand the problem in more depth."

The caricature example collects most of the ways it goes wrong at once:

```cpp
struct X {
    char ch;
    int i;
    string s;
    char ch2;
    X& operator=(const X& a);
    X(const X&);
};
X waste(const char* p)
{
    if (!p) throw Nullptr_error{};
    int n = strlen(p);
    auto buf = new char[n];
    if (!buf) throw Allocation_error{};
    for (int i = 0; i < n; ++i) buf[i] = p[i];
    // ... manipulate buffer ...
    X x;
    x.ch = 'a';
    x.s = string(n);    // give x.s space for *p
    for (gsl::index i = 0; i < x.s.size(); ++i) x.s[i] = buf[i];
    delete[] buf;
    return x;
}
```

Every individual mistake here has been seen in production code, and worse. The
layout of `X` guarantees that at least 6 bytes, and most likely more, are
wasted. The spurious definition of the copy operations disables move semantics,
so the return is slow — the return value optimisation is not guaranteed here.
The use of `new` and `delete` for `buf` is redundant; if a local string were
really needed, a local `string` should have been used.

The second example is real production code:

```cpp
void lower(zstring s)
{
    for (int i = 0; i < strlen(s); ++i) s[i] = tolower(s[i]);
}
```

The condition contains `i < strlen(s)`, which is evaluated on every iteration,
so `strlen` walks the whole string every time round the loop. The contents
change but the length does not, so the length should be computed once outside
the loop.

An individual example of waste is rarely significant, and where it is
significant it is typically easy for an expert to eliminate. However, waste
spread liberally across a code base can easily be significant, and experts are
not always as available as one would like. The aim of this rule, and of the more
specific rules that support it, is to eliminate most waste related to the *use
of C++* before it happens. Waste related to algorithms and requirements is a
separate matter, and beyond the scope of these guidelines.

## Decision

Do not waste time or space in the ordinary use of the language.

- Do not declare copy or move operations that the compiler would generate
  correctly; a hand-written copy constructor suppresses moves.
- Do not leave a loop-invariant computation in the loop condition, especially
  one that is O(n) like `strlen`; compute it once before the loop.
- Do not allocate a buffer to build something a standard container or string
  could have built directly, and do not copy data twice to get it into place.
- Order data members so the type does not pay for avoidable padding, where the
  type is used in bulk.
- Return by value and let moves and copy elision do their work.

This rule is about waste that comes from how C++ is used, not about optimising
algorithms. Spending time or space deliberately, to buy safety, clarity or
testability, is not waste and is not covered by it.

## Rationale

The reason to write this down is not any single site — a competent reviewer
catches the `strlen` in the loop condition — but the aggregate. Waste of this
kind is uniformly distributed, individually below the threshold at which anyone
profiles, and collectively enough to make a program feel slow with no hotspot to
point at.

The carve-out is what keeps the rule from being harmful. Read without it, "do
not waste" becomes an argument against bounds checking, against RAII, against
meaning-carrying types — against most of the rest of this section.

## Consequences

### Positive

- Avoidable costs are removed at the point of writing, where they are free to
  avoid, rather than hunted later.
- Not writing the copy operations keeps move semantics and gets the fast return
  for nothing.
- Thinking about cost tends to produce a better understanding of the problem, as
  the quote above claims.

### Negative

- This rule is a standing invitation to premature optimisation, and the line
  between "waste" and "not yet measured" is a matter of judgement.
- Member reordering for padding fights with grouping members by meaning, which
  is a readability cost paid for a space gain that is often irrelevant.
- Reviewing for waste costs review time, on changes that are already correct.

### Neutral

- Algorithmic and requirements-level efficiency is explicitly out of scope.
- No profiling or benchmarking practice is established by this record.

## Enforcement

Many more specific rules aim at the overall goals of simplicity and elimination
of gratuitous waste. Mechanically:

- Flag an unused return value from a user-defined non-defaulted postfix
  `operator++` or `operator--`; prefer the prefix form. ("User-defined
  non-defaulted" is intended to reduce noise.)

## Related

- [0014](0014-p-8-do-not-leak-any-resources.md) — the correctness half of
  resource use.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — a good library is
  usually faster than the hand-rolled version, as well as more correct.
- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — immutability
  sometimes enables better optimisation.
