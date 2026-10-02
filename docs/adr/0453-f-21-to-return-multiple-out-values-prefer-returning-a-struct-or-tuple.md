# 0453. F.21: To return multiple "out" values, prefer returning a struct or tuple

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

A return value is self-documenting as an output-only value. C++ does have
multiple return values, by the convention of using a tuple — including `pair` —
possibly with the extra convenience of `tie` or of structured bindings at the
call site. Prefer a named struct where there are semantics to the returned
value; otherwise a nameless tuple is useful in generic code.

```cpp
// BAD: output-only parameter documented in a comment
int f(const string& input, /*output only*/ string& output_data)
{
    // ...
    output_data = something();
    return status;
}

// GOOD: self-documenting
tuple<int, string> f(const string& input)
{
    // ...
    return make_tuple(status, something());
}
```

C++98's standard library already used this style, because a `pair` is a
two-element tuple. Given a `set<string> my_set`:

```cpp
// C++98
result = my_set.insert("Hello");
if (result.second) do_something_with(result.first);    // workaround
```

C++11 allowed the results to go directly into existing local variables:

```cpp
Sometype iter;                                 // default initialize if we haven't already
Someothertype success;                         // used these variables for some other purpose
tie(iter, success) = my_set.insert("Hello");   // normal return value
if (success) do_something_with(iter);
```

C++17 structured bindings declare and initialise the variables in one step:

```cpp
if (auto [ iter, success ] = my_set.insert("Hello"); success) do_something_with(iter);
```

There is an exception. Sometimes an object must be passed to a function so that
its state can be manipulated, and passing by reference is then the right
technique; explicitly passing an in-out parameter back out again as a return
value is usually unnecessary:

```cpp
istream& operator>>(istream& is, string& s);    // much like std::operator>>()

for (string s; cin >> s; ) {
    // do something with the string
}
```

Both `s` and `cin` are in-out parameters here. `cin` is passed by non-`const`
reference so that its state can be manipulated, and `s` is passed to avoid
repeated allocations: reusing `s` allocates new memory only when its capacity
must grow. This is sometimes called the caller-allocated out pattern, and it is
particularly useful for types such as `string` and `vector` that allocate on the
free store. Passing everything out as return values instead would give:

```cpp
pair<istream&, string> get_string(istream& is)  // not recommended
{
    string s;
    is >> s;
    return {is, s};
}

for (auto p = get_string(cin); p.first; ) {
    // do something with p.second
}
```

which is significantly less elegant and significantly less performant. On a
strict reading the exception is not really an exception, because it relies on
in-out parameters rather than the plain out parameters the rule is about; the
guidelines prefer to be explicit rather than subtle about it.

A specific, user-defined type is often the better choice:

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

The overly generic `pair` and `tuple` should be used only when the returned
value represents independent entities rather than an abstraction. Along the same
lines, a specific type such as `variant<T, error_code>` beats a generic `tuple`.

## Decision

Return several output values as one object, and name that object when it means
something.

- Define a struct with named members when the values together form an
  abstraction, and return it.
- Use `tuple` or `pair` only where the values are genuinely independent, or in
  generic code where no meaningful name exists.
- Destructure at the call site with structured bindings.
- Do not add output parameters, and do not document an output parameter with a
  comment instead of changing the signature.
- Keep passing an object by reference where the function manipulates its state,
  or where a capacity-carrying buffer is being reused across calls; that is an
  in-out parameter and this rule does not apply to it.
- Prefer a purpose-built type such as `variant<T, error_code>` over a generic
  pair of value and status.

## Rationale

The struct-versus-tuple half of the rule is what makes it worth writing down,
because the tuple only moves the documentation problem. `tuple<int, string>`
tells a caller as little about the meaning of its elements as `string& output`
did, and structured bindings let the caller invent names the author never
intended: `auto [x, y] = measure(obj4)` is legal and wrong. A named struct puts
the meaning in one place where it can be checked, and gives the group a type
that can later grow a member without breaking every call site.

## Consequences

### Positive

- The signature says which values come out, without a comment.
- Callers cannot forget to inspect an output, because there is no
  default-initialised variable waiting to be overwritten.
- A named struct can gain an invariant, a member function, or a default, none of
  which a tuple can carry.
- Structured bindings make the call site as short as the output-parameter
  version was.

### Negative

- A struct per multi-valued function is real boilerplate, and it has to live
  somewhere visible to both sides, which grows headers.
- Structured binding names are chosen by the caller and are unchecked, so a
  tuple return actively invites the confusion the rule warns about.
- Adding a member to a returned struct is a source and ABI change for every
  caller, whereas an extra output parameter could once be added as an overload.
- The exception is broad in practice: the caller-allocated out pattern covers
  most of the performance-sensitive cases, so the rule bites least where the
  argument for it would be strongest.

### Neutral

- `pair` returned by the standard library, as from `set::insert`, is the same
  pattern and predates the rule.
- The `variant<T, error_code>` shape the rule ends by recommending is the same
  pattern applied to a value-or-failure result, not a separate rule.

## Enforcement

- Output parameters should be replaced by return values. An output parameter is
  one that the function writes to, invokes a non-`const` member function on, or
  passes on as a non-`const`.

## Related

- [0452](0452-f-20-for-out-output-values-prefer-return-values-to-output-parameters.md)
  — the single-value form of the same rule.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — why the
  named struct beats the tuple.
- [0447](0447-f-15-prefer-simple-and-conventional-ways-of-passing-information.md)
  — the umbrella rule, including the caller-allocated out exception.
