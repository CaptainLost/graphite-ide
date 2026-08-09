# 0040. F.1: Package meaningful operations as carefully named functions

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

The rule is stated upstream as `"Package" meaningful operations as carefully
named functions`; the title here drops the quotation marks so that the filename
stays clean.

## Context

The first rule of the F.def subsection. It bears most on `Sln` and `Text`, where
parsing and editing routines have grown long enough that the sub-operations
inside them are unnamed.

## Decision

Adopt F.1 as written. Transcribed in full:

> **Reason.** Factoring out common code makes code more readable, more likely to
> be reused, and limit errors from complex code. If something is a
> well-specified action, separate it out from its surrounding code and give it a
> name.

**Example, don't.**

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

Almost everything is wrong with `read_and_print`. It reads, it writes (to a
fixed `ostream`), it writes error messages (to a fixed `ostream`), it handles
only `int`s. There is nothing to reuse, logically separate operations are
intermingled and local variables are in scope after the end of their logical
use. For a tiny example, this looks OK, but if the input operation, the output
operation, and the error handling had been more complicated the tangled mess
could become hard to understand.

**Note.** If you write a non-trivial lambda that potentially can be used in more
than one place, give it a name by assigning it to a (usually non-local)
variable.

**Example.**

```cpp
sort(a, b, [](T x, T y) { return x.rank() < y.rank() && x.value() < y.value(); });
```

Naming that lambda breaks up the expression into its logical parts and provides
a strong hint to the meaning of the lambda.

```cpp
auto lessT = [](T x, T y) { return x.rank() < y.rank() && x.value() < y.value(); };

sort(a, b, lessT);
```

The shortest code is not always the best for performance or maintainability.

**Exception.** Loop bodies, including lambdas used as loop bodies, rarely need to
be named. However, large loop bodies (e.g., dozens of lines or dozens of pages)
can be a problem. The rule Keep functions short and simple implies "Keep loop
bodies short." Similarly, lambdas used as callback arguments are sometimes
non-trivial, yet unlikely to be reusable.

**Enforcement.**

- See Keep functions short and simple
- Flag identical and very similar lambdas used in different places.

## Rationale

Adopted as written. It is the general statement of which F.2 and F.3 are the
testable consequences, and it defers its own enforcement to F.3, so the
inventory of oversized functions is kept in
[0042](0042-f-3-keep-functions-short-and-simple.md) rather than duplicated here.

The part that adds something beyond F.2 and F.3 is the Note about lambdas: a
non-trivial lambda used more than once should be given a name. That is a cheap,
local change, unlike breaking up a 200-line parser.

## Consequences

### Positive

- Gives the review comment "this block wants a name" a written basis.
- The lambda Note is actionable in a way the rest of the rule is not.

### Negative

- Naming has a cost the rule does not mention: each extracted function is a name
  to learn and a place to look, and a file of ten three-line helpers can be
  harder to follow than one twenty-line function with blank lines.
- The rule cannot say when an operation is "well-specified" enough to deserve a
  name, so it settles no actual argument.

### Neutral

- Enforcement is deferred to F.3, which is where the concrete threshold lives.

## Related

- [0041](0041-f-2-a-function-should-perform-a-single-logical-operation.md) — the
  next rule.
- [0042](0042-f-3-keep-functions-short-and-simple.md) — where this rule's
  enforcement is kept.
- [0009](0009-p-3-express-intent.md) — P.3, of which this is the function-shaped
  case.
