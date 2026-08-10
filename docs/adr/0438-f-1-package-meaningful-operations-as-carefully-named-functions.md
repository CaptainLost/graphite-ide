# 0438. F.1: "Package" meaningful operations as carefully named functions

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Factoring out common code makes code more readable, more likely to be reused,
and limits the errors that come from complex code. If something is a
well-specified action, separate it out from its surrounding code and give it a
name.

```cpp
void read_and_print(istream& is)    // read and print an int
{
    int x;
    if (is >> x)
        cout << "the int is " << x << '\n';
    else
        cerr << "no int on input\n";
}
```

Almost everything is wrong with `read_and_print`. It reads, it writes to a fixed
`ostream`, it writes error messages to another fixed `ostream`, and it handles
only `int`. There is nothing to reuse, logically separate operations are
intermingled, and local variables remain in scope after the end of their logical
use. For a tiny example this looks acceptable, but if the input operation, the
output operation and the error handling had been more complicated, the tangled
mess would become hard to understand.

The rule extends to lambdas. If you write a non-trivial lambda that could
plausibly be used in more than one place, give it a name by assigning it to a
variable, usually a non-local one:

```cpp
sort(a, b, [](T x, T y) { return x.rank() < y.rank() && x.value() < y.value(); });

auto lessT = [](T x, T y) { return x.rank() < y.rank() && x.value() < y.value(); };
sort(a, b, lessT);
```

Naming that lambda breaks the expression into its logical parts and gives a
strong hint about its meaning. The shortest code is not always the best for
performance or for maintainability.

There is an exception. Loop bodies, including lambdas used as loop bodies,
rarely need to be named, though a loop body that runs to dozens of lines is a
problem in its own right — the rule that functions stay short and simple implies
that loop bodies stay short too. Lambdas passed as callback arguments are
sometimes non-trivial and yet unlikely to ever be reusable.

## Decision

Give every well-specified action a name and a function of its own.

- When a block of code inside a larger function can be described in a short
  phrase, extract it into a function whose name is that phrase.
- Do not intermingle logically separate operations — reading, computing,
  formatting, reporting — in one body; each becomes its own named operation.
- Give a non-trivial lambda a name by binding it to a variable when it could be
  used more than once, or when naming it explains the expression it appears in.
- Leave short loop bodies and one-off callback lambdas unnamed; the rule is
  about reusable meaning, not about banning anonymity.
- Treat two identical or near-identical lambdas in different places as a missing
  named function.

## Rationale

The sharpest argument here is not reuse, it is scope. In `read_and_print` the
variable `x` outlives the operation that gave it meaning, and every later line
of the function can still see it; extracting the operation ends the variable's
life with its purpose. Naming is the cheapest available form of documentation
and the only one the compiler helps keep honest, because a name that no longer
fits its body is visible at every call site rather than in a comment nobody
reads.

## Consequences

### Positive

- Operations become individually testable, because they can be called on their
  own with arguments the test chooses.
- Local variables stop leaking past the code that gave them meaning.
- Duplicated logic becomes visible: two calls to one name rather than two
  similar-looking blocks.
- A good name documents intent at every call site, not just at the definition.

### Negative

- Extraction is not free: a small operation pulled out of a body often needs
  several parameters to replace the locals it used to close over, and the
  signature can end up harder to read than the original block.
- Naming is genuinely hard, and a badly named function is worse than an inline
  block because it actively misleads.
- More, smaller functions means more indirection to follow when reading a call
  chain for the first time.
- The rule offers no threshold, so "well-specified action" is a judgement that
  two reviewers will draw in different places.

### Neutral

- Whether the extracted function is a free function, a member, or a named lambda
  is decided elsewhere; this record only says that it has a name.
- Nothing here says the extracted function must be non-local; a named lambda in
  the same scope satisfies the rule.

## Enforcement

- Apply the enforcement of the rule that functions stay short and simple.
- Flag identical and very similar lambdas used in different places.

## Related

- [0439](0439-f-2-a-function-should-perform-a-single-logical-operation.md) — the
  same argument stated as a property the resulting function must have.
- [0440](0440-f-3-keep-functions-short-and-simple.md) — supplies the size
  threshold this rule deliberately leaves open.
- [0009](0009-p-3-express-intent.md) — a name is the primary way intent is
  expressed in code.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — packaging an operation is the mild case of the same move.
