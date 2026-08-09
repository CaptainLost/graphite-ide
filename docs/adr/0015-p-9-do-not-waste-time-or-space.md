# 0015. P.9: Do not waste time or space

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

The rule is stated upstream as "Don't waste time or space"; the title here is
spelled without the apostrophe so that the filename stays clean.

## Context

The performance-shaped philosophy rule. It bears on the parts of this program
that run every frame — the panel render path — and on the file tree, which can
hold a large directory.

## Decision

Adopt P.9 as written. Transcribed in full:

> **Reason.** This is C++.

**Note.** Time and space that you spend well to achieve a goal (e.g., speed of
development, resource safety, or simplification of testing) is not wasted.
"Another benefit of striving for efficiency is that the process forces you to
understand the problem in more depth." — Alex Stepanov

**Example, bad.**

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
    for (gsl::index i = 0; i < x.s.size(); ++i) x.s[i] = buf[i];  // copy buf into x.s
    delete[] buf;
    return x;
}

void driver()
{
    X x = waste("Typical argument");
    // ...
}
```

Yes, this is a caricature, but we have seen every individual mistake in
production code, and worse. Note that the layout of `X` guarantees that at least
6 bytes (and most likely more) are wasted. The spurious definition of copy
operations disables move semantics so that the return operation is slow (please
note that the Return Value Optimization, RVO, is not guaranteed here). The use
of `new` and `delete` for `buf` is redundant; if we really needed a local
string, we should use a local `string`. There are several more performance bugs
and gratuitous complication.

**Example, bad.**

```cpp
void lower(zstring s)
{
    for (int i = 0; i < strlen(s); ++i) s[i] = tolower(s[i]);
}
```

This is actually an example from production code. We can see that in our
condition we have `i < strlen(s)`. This expression will be evaluated on every
iteration of the loop, which means that `strlen` must walk through string every
loop to discover its length. While the string contents are changing, it's
assumed that `tolower` will not affect the length of the string, so it's better
to cache the length outside the loop and not incur that cost each iteration.

**Note.** An individual example of waste is rarely significant, and where it is
significant, it is typically easily eliminated by an expert. However, waste
spread liberally across a code base can easily be significant and experts are
not always as available as we would like. The aim of this rule (and the more
specific rules that support it) is to eliminate most waste related to the use of
C++ before it happens. After that, we can look at waste related to algorithms
and requirements, but that is beyond the scope of these guidelines.

**Enforcement.** Many more specific rules aim at the overall goals of simplicity
and elimination of gratuitous waste.

- Flag an unused return value from a user-defined non-defaulted postfix
  `operator++` or `operator--` function. Prefer using the prefix form instead.
  (Note: "User-defined non-defaulted" is intended to reduce noise. Review this
  enforcement if it's still too noisy in practice.)

## Rationale

Adopted, with the Note that qualifies it treated as part of the rule rather
than as a footnote: time spent on resource safety or on making something
testable is not waste. That qualification is what keeps this rule from being
used to argue against `Result<T>`, against `NonCopyable`, or against splitting
Core from Graphite — all of which cost something and buy something named.

## Consequences

### Positive

- Names the specific mistakes worth watching: repeated work in a loop
  condition, copies that should be moves, and a copy constructor that silently
  disables move semantics.

### Negative

- As written it is unfalsifiable. It cannot reject a change without a
  measurement, and this repository has no benchmark, so in practice it is an
  attitude rather than a check. Cited without a number, it is an aesthetic
  argument wearing a rule's clothes.
- It invites premature optimisation in exactly the code where it is least
  needed — the render path is bounded by vsync, not by our loops.

### Neutral

- The one mechanical enforcement it offers, about postfix `operator++`, has no
  subject in this codebase: no user-defined increment operator exists.

## Deviations to fix

None recorded. Identifying "waste" honestly requires a measurement, and none has
been taken; listing suspicions here would be guesswork dressed as an inventory.
The known deliberate cost is the continuous redraw loop — the application
renders at full rate when idle — which was accepted knowingly in the original
design and is a candidate for its own record if it is ever revisited.

## Related

- [0014](0014-p-8-do-not-leak-any-resources.md) — the previous rule.
- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — the next rule.
