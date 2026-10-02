# 0439. F.2: A function should perform a single logical operation

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

A function that performs a single operation is simpler to understand, to test,
and to reuse. Consider:

```cpp
void read_and_print()    // bad
{
    int x;
    cin >> x;
    // check for errors
    cout << x << "\n";
}
```

This is a monolith that is tied to a specific input and will never find another
use. Break the function up into suitable logical parts and parameterise them:

```cpp
int read(istream& is)    // better
{
    int x;
    is >> x;
    // check for errors
    return x;
}

void print(ostream& os, int x)
{
    os << x << "\n";
}
```

These can now be combined where needed:

```cpp
void read_and_print()
{
    auto x = read(cin);
    print(cout, x);
}
```

If there were a need, `read()` and `print()` could be further templatised on the
data type, the I/O mechanism, or the response to errors:

```cpp
auto read = [](auto& input, auto& value)    // better
{
    input >> value;
    // check for errors
};

auto print(auto& output, const auto& value)
{
    output << value << "\n";
}
```

## Decision

Make each function do one thing, at one level of abstraction.

- If the function's honest name contains "and", it is two functions.
- Separate acquiring a value from acting on it, and separate computing a result
  from reporting it.
- Parameterise the things the monolith had hard-coded: the stream, the
  destination, the error response, the element type.
- Treat more than one "out" parameter as evidence that the function is doing two
  operations; return the values instead.
- Treat a function that does not fit on one editor screen as suspicious, and
  factor it into smaller, well-named sub-operations.

## Rationale

The reusability argument is the visible one, but the testability argument is the
one that decides. `read_and_print()` cannot be tested without a console; `read`
and `print` can each be tested against a `stringstream`. A function fused to its
input and its output has no seam where a test can be attached, and no seam is
where the untested code accumulates. Splitting also makes each half's error
handling a separate decision instead of one tangled one.

## Consequences

### Positive

- Each half becomes independently testable, because its inputs and outputs are
  parameters rather than globals.
- The pieces recombine into uses the original author did not anticipate.
- The error-handling policy for each operation is decided once, in one place.
- Reviewing becomes local: a reader can judge `read` without reading `print`.

### Negative

- "A single logical operation" is not a definable unit; the split point is a
  judgement, and applied too eagerly it produces a fog of one-line functions
  that are individually trivial and collectively unreadable.
- The composed version does more work at the boundary: a value that was a local
  now travels as a return value and an argument.
- Splitting often forces a decision about which half owns a resource that
  neither half owned before.
- Parameterising the stream, the type and the error policy at once is how a
  simple function becomes a template with four parameters nobody instantiates
  differently.

### Neutral

- This rule says nothing about how large the single operation may be; that is
  the subject of a separate record.
- The recombined `read_and_print()` is still allowed to exist as a convenience;
  it is now composition rather than a monolith.

## Enforcement

- Consider functions with more than one "out" parameter suspicious. Use return
  values instead, including a tuple for multiple return values.
- Consider "large" functions that do not fit on one editor screen suspicious,
  and consider factoring them into smaller, well-named sub-operations.
- Consider functions with seven or more parameters suspicious.

## Related

- [0438](0438-f-1-package-meaningful-operations-as-carefully-named-functions.md)
  — the extraction move; this record states the test for whether it is done.
- [0440](0440-f-3-keep-functions-short-and-simple.md) — the size limit that
  usually reveals the second operation.
- [0452](0452-f-20-for-out-output-values-prefer-return-values-to-output-parameters.md)
  — why the multiple-"out"-parameter symptom is a symptom.
- [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) — the general
  rule behind the seven-parameter heuristic in the enforcement above.
