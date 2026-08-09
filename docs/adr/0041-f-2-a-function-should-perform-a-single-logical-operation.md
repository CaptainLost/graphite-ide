# 0041. F.2: A function should perform a single logical operation

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

`Application::Run()` is the clearest counter-example in this codebase: it
creates the window, initialises the GUI, runs the frame loop and shuts
everything down, in eighty lines.

## Decision

Adopt F.2 as written. Transcribed in full:

> **Reason.** A function that performs a single operation is simpler to
> understand, test, and reuse.

**Example.** Consider:

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
(different) use. Instead, break functions up into suitable logical parts and
parameterize:

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

If there was a need, we could further templatize `read()` and `print()` on the
data type, the I/O mechanism, the response to errors, etc. Example:

```cpp
auto read = [](auto& input, auto& value)    // better
{
    input >> value;
    // check for errors
};

void print(auto& output, const auto& value)
{
    output << value << "\n";
}
```

**Enforcement.**

- Consider functions with more than one "out" parameter suspicious. Use return
  values instead, including `tuple` for multiple return values.
- Consider "large" functions that don't fit on one editor screen suspicious.
  Consider factoring such a function into smaller well-named suboperations.
- Consider functions with 7 or more parameters suspicious.

## Rationale

Adopted as written. Two of its three enforcement points pass here already: no
function has seven parameters — the maximum anywhere is three, per
[0034](0034-i-23-keep-the-number-of-function-arguments-low.md) — and out
parameters are rare because failures return `Result<T>`.

The third point is the live one, and its inventory is kept in
[0042](0042-f-3-keep-functions-short-and-simple.md).

The rule's real content for this codebase is the word *logical*: a long function
that does one thing is a different problem from a short one that does three.
`Application::Run()` is long **and** does four things, which is why it is named
here; `ParseSln` is longer still but does one thing, and belongs under F.3
rather than under this rule.

## Consequences

### Positive

- Separates two complaints that get conflated — length and cohesion — so a
  review can say which one it means.
- The out-parameter point is already satisfied by the `Result<T>` scheme, so
  this rule reinforces a decision already made.

### Negative

- "Single logical operation" is a judgement with no test. `Run()` can be
  defended as "run the application", which is one operation at the right
  altitude and four at any lower one.

### Neutral

- The templatized variant in the last example is not a direction this codebase
  takes; generic parameterisation is not the point of the rule here.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| `Application::Run()` creates the window, initialises the GUI, runs the frame loop and shuts down — four operations in one function | `Core/src/Core/App/Application.cpp:41-121` | Cohesion, not just length; the frame loop is the part that wants extracting |

## Related

- [0040](0040-f-1-package-meaningful-operations-as-carefully-named-functions.md)
  — the previous rule.
- [0042](0042-f-3-keep-functions-short-and-simple.md) — the next rule, which
  carries the length inventory.
- [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) — the parameter
  count this rule also mentions.
