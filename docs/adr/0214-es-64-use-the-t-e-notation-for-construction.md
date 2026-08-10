# 0214. ES.64: Use the T{e} notation for construction

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

The `T{e}` construction syntax makes it explicit that construction is desired.
It does not allow narrowing, and it is the only safe and general expression for
constructing a value of type `T` from an expression `e`. The cast notations
`T(e)` and `(T)e` are neither safe nor general.

For built-in types, the construction notation protects against narrowing and
against reinterpretation:

```cpp
void use(char ch, int i, double d, char* p, long long lng)
{
    int x1 = int{ch};     // OK, but redundant
    int x2 = int{d};      // error: double->int narrowing; use a cast if you need to
    int x3 = int{p};      // error: pointer to->int; use a reinterpret_cast if you really need to
    int x4 = int{lng};    // error: long long->int narrowing; use a cast if you need to

    int y1 = int(ch);     // OK, but redundant
    int y2 = int(d);      // bad: double->int narrowing; use a cast if you need to
    int y3 = int(p);      // bad: pointer to->int; use a reinterpret_cast if you really need to
    int y4 = int(lng);    // bad: long long->int narrowing; use a cast if you need to

    int z1 = (int)ch;     // OK, but redundant
    int z2 = (int)d;      // bad: double->int narrowing; use a cast if you need to
    int z3 = (int)p;      // bad: pointer to->int; use a reinterpret_cast if you really need to
    int z4 = (int)lng;    // bad: long long->int narrowing; use a cast if you need to
}
```

The integer-to-pointer and pointer-to-integer conversions are
implementation-defined under the `T(e)` and `(T)e` notations, and non-portable
between platforms with different integer and pointer sizes.

Where the type is unambiguous, the `T` can be left out of `T{e}`:

```cpp
complex<double> f(complex<double>);

auto z = f({2*pi, 1});
```

There is an exception. `std::vector` and the other containers were defined
before `{}` existed as a construction notation:

```cpp
vector<string> vs {10};                           // ten empty strings
vector<int> vi1 {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};  // ten elements 1..10
vector<int> vi2 {10};                             // one element with the value 10
```

How, then, do we get a vector of ten default-initialised `int`s?

```cpp
vector<int> v3(10); // ten elements with value 0
```

Using `()` rather than `{}` for the number of elements is conventional, going
back to the early 1980s, and hard to change — but it is still a design error.
For a container whose element type can be confused with the number of elements
there is an ambiguity that must be resolved, and the conventional resolution is
to read `{10}` as a list of one element and `(10)` as a size. This mistake need
not be repeated in new code; a type can be defined to represent the count:

```cpp
struct Count { int n; };

template<typename T>
class Vector {
public:
    Vector(Count n);                     // n default-initialized elements
    Vector(initializer_list<T> init);    // init.size() elements
    // ...
};

Vector<int> v1{10};
Vector<int> v2{Count{10}};
Vector<Count> v3{Count{10}};    // yes, there is still a very minor problem
```

The main problem left is finding a suitable name for `Count`.

## Decision

Construct with braces, and reserve the parenthesised and C-style forms for the
cases the language forces.

- Write `T{e}` to construct a `T` from an expression `e`. Do not write `T(e)`,
  and do not write `(T)e`; see
  [0205](0205-es-48-avoid-casts.md).
- Leave the `T` out — `f({2*pi, 1})` — where the target type is unambiguous from
  the context.
- Where a conversion is genuinely lossy and intended, do not reach back for the
  parenthesised form. Say so with a named narrowing operation, per
  [0203](0203-es-46-avoid-lossy-narrowing-truncating-arithmetic-conversions.md).
- Use `()` for the standard containers where the argument is a count rather than
  an element — `vector<int> v(10)` — because the standard library's
  `initializer_list` constructors make `{}` mean something else there.
- When defining a new type whose element type could be confused with a count, do
  not repeat the standard library's ambiguity; give the count its own named
  type.

## Rationale

The three columns of the example are the whole argument. `int{d}`, `int(d)` and
`(int)d` are the same conversion written three ways, and only one of them
refuses to compile when the conversion loses information. The braces are not a
style preference; they are the difference between a construction that is checked
and a cast that is not.

The second-order value is uniformity. `T{e}` works for built-in types, class
types, aggregates and containers alike, so there is one notation to learn and
one shape to recognise, rather than a per-category rule about which parentheses
mean construction and which mean conversion.

## Consequences

### Positive

- Narrowing conversions written by accident become compile errors at the point
  they are written.
- Pointer-to-integer reinterpretation stops being reachable by ordinary-looking
  syntax and requires an explicit `reinterpret_cast`.
- One notation covers construction of every kind of type, which removes the
  most-vexing-parse problem that `T x(...)` can produce.

### Negative

- The container exception is not a corner case; `vector`, `string` and their
  relatives are among the most-used types in any codebase, so the rule in
  practice reads "always use braces, except in the places you use most often".
- `{}` prefers an `initializer_list` constructor whenever one is viable, which
  can silently select a different constructor from the one intended — and for a
  type whose element type is an integer, the wrong one.
- `auto x{e}` has its own history and does not mean what the rule's shape
  suggests; the notation is not uniform once `auto` is involved.
- Refusing to narrow means legitimate truncation needs extra ceremony, so the
  rule increases the amount of code written at exactly the arithmetic-heavy
  sites where readability is already under pressure.

### Neutral

- The `Count` design suggested for new container-like types is advice about a
  rare situation; most types never face the ambiguity.
- This record governs the notation, not whether the conversion should happen at
  all; that is
  [0203](0203-es-46-avoid-lossy-narrowing-truncating-arithmetic-conversions.md)'s
  subject.

## Enforcement

Flag the C-style `(T)e` and functional-style `T(e)` casts.

## Related

- [0205](0205-es-48-avoid-casts.md) — the rule that sends the functional-style
  cast here.
- [0206](0206-es-49-if-you-must-use-a-cast-use-a-named-cast.md) — what to write
  when the conversion really is a cast.
- [0203](0203-es-46-avoid-lossy-narrowing-truncating-arithmetic-conversions.md)
  — the narrowing that braces refuse.
- [0183](0183-es-20-always-initialize-an-object.md) — the related requirement
  that the object be given a value at all.
