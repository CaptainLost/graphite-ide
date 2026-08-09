# 0057. F.21: To return multiple out values, prefer returning a struct

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

The rule that says what F.20 should return. It bears on
`Window::FramebufferSize(int&, int&)`, the one out-parameter pair in the
codebase, and it endorses a pattern already in use: `Result<T>` is a named type
carrying a value and an outcome, which is exactly what the rule's final Note
recommends over `pair<T, error_code>`.

## Decision

Adopt F.21 as written. Transcribed in full:

> **Reason.** A return value is self-documenting as an "output-only" value. Note
> that C++ does have multiple return values, by convention of using tuple-like
> types (struct, array, tuple, etc.), possibly with the extra convenience of
> structured bindings (C++17) at the call site. Prefer using a named struct if
> possible. Otherwise, a tuple is useful in variadic templates.

**Example.**

```cpp
// BAD: output-only parameter documented in a comment
int f(const string& input, /*output only*/ string& output_data)
{
    // ...
    output_data = something();
    return status;
}

// GOOD: self-documenting
struct f_result { int status; string data; };

f_result f(const string& input)
{
    // ...
    return {status, something()};
}
```

C++98's standard library used this style in places, by returning `pair` in some
functions. For example, given a `set<string> my_set`, consider:

```cpp
// C++98
pair<set::iterator, bool> result = my_set.insert("Hello");
if (result.second)
    do_something_with(result.first);    // workaround
```

With C++17 we are able to use "structured bindings" to give each member a name:

```cpp
if (auto [ iter, success ] = my_set.insert("Hello"); success)
    do_something_with(iter);
```

A struct with meaningful names is more common in modern C++. See for example
`ranges::min_max_result`, `from_chars_result`, and others.

**Exception.** Sometimes, we need to pass an object to a function to manipulate
its state. In such cases, passing the object by reference `T&` is usually the
right technique. Explicitly passing an in-out parameter back out again as a
return value is often not necessary. For example:

```cpp
istream& operator>>(istream& in, string& s);    // much like std::operator>>()

for (string s; in >> s; ) {
    // do something with line
}
```

Here, both `s` and `in` are used as in-out parameters. We pass `in` by
(non-`const`) reference to be able to manipulate its state. We pass `s` to avoid
repeated allocations. By reusing `s` (passed by reference), we allocate new
memory only when we need to expand `s`'s capacity. This technique is sometimes
called the "caller-allocated out" pattern and is particularly useful for types,
such as `string` and `vector`, that need to do free store allocations.

To compare, if we passed out all values as return values, we would write
something like this:

```cpp
struct get_string_result { istream& in; string s; };

get_string_result get_string(istream& in)  // not recommended
{
    string s;
    in >> s;
    return { in, move(s) };
}

for (auto [in, s] = get_string(cin); in; s = get_string(in).s) {
    // do something with string
}
```

We consider that significantly less elegant with significantly less performance.

For a truly strict reading of this rule (F.21), the exception isn't really an
exception because it relies on in-out parameters, rather than the plain out
parameters mentioned in the rule. However, we prefer to be explicit, rather than
subtle.

**Note.** In most cases, it is useful to return a specific, user-defined type.
For example:

```cpp
struct Distance {
    int value;
    int unit = 1;   // 1 means meters
};

Distance d1 = measure(obj1);        // access d1.value and d1.unit
auto d2 = measure(obj2);            // access d2.value and d2.unit
auto [value, unit] = measure(obj3); // access value and unit; somewhat redundant
                                    // to people who know measure()
auto [x, y] = measure(obj4);        // don't; it's likely to be confusing
```

The overly generic `pair` and `tuple` should be used only when the value
returned represents independent entities rather than an abstraction.

Another option is to use `optional<T>` or `expected<T, error_code>`, rather than
`pair` or `tuple`. When used appropriately these types convey more information
about what the members mean than `pair<T, bool>` or `pair<T, error_code>` do.

**Note.** When the object to be returned is initialized from local variables that
are expensive to copy, explicit move may be helpful to avoid copying:

```cpp
pair<LargeObject, LargeObject> f(const string& input)
{
    LargeObject large1 = g(input);
    LargeObject large2 = h(input);
    // ...
    return { move(large1), move(large2) }; // no copies
}
```

Alternatively,

```cpp
pair<LargeObject, LargeObject> f(const string& input)
{
    // ...
    return { g(input), h(input) }; // no copies, no moves
}
```

Note this is different from the `return move(...)` anti-pattern from ES.56.

**Enforcement.**

- Output parameters should be replaced by return values. An output parameter is
  one that the function writes to, invokes a non-`const` member function, or
  passes on as a non-`const`.
- `pair` or `tuple` return types should be replaced by `struct`, if possible. In
  variadic templates, `tuple` is often unavoidable.

## Rationale

Adopted as written. The rule's preference for a named type over `pair` or
`tuple` is the reason `Result<T>` exists in the form it does rather than as
`pair<T, Error>`, so this rule and
[0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md)
agree — the guidelines' own suggestion of `expected<T, error_code>` is what
`Result<T>` is standing in for until C++23.

The second enforcement point passes here: no function returns a `pair` or
`tuple`. Small named structs are already the codebase's habit — `ProjectContent`,
`OpenOutcome`, `Error`.

The Exception is worth keeping in view for the text-editing code, where the
caller-allocated-out pattern would avoid an allocation per keystroke. It is not
used today and this record does not ask for it, but the rule sanctions it if a
measurement ever calls for it.

## Consequences

### Positive

- Endorses the shape the codebase already uses and gives it a name, so a future
  `pair` return has an argument against it.
- Structured bindings make a named-struct return no more verbose at the call
  site than a `tuple`.

### Negative

- A named struct per multi-value return means a type per function, and several
  of these types are used exactly once. That is a real cost in header surface,
  paid for a naming benefit.
- The Exception's caller-allocated-out pattern is genuinely faster in a loop, and
  preferring returns forgoes it silently.

### Neutral

- `optional<T>` is available and underused here; `Result<T>` covers the
  value-or-error case, but "value or nothing, no error" is currently expressed
  in various ad-hoc ways.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| `FramebufferSize(int& width, int& height)` should return a named struct — a `FramebufferSize` or `Size` with `width` and `height` | `Core/src/Core/Platform/Window.h:37` | Same finding as [0056](0056-f-20-for-out-output-values-prefer-return-values-to-output-parameters.md); this record says what the replacement should be |

## Related

- [0056](0056-f-20-for-out-output-values-prefer-return-values-to-output-parameters.md)
  — the previous rule, which finds the case.
- [0058](0058-f-60-prefer-t-over-t-when-no-argument-is-a-valid-option.md) — the
  next rule.
- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  `Result<T>` as the named alternative to `pair<T, error_code>`.
