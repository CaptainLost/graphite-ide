# 0186. ES.23: Prefer the {}-initializer syntax

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

The rules for `{}` initialisation are simpler, more general, less ambiguous and
safer than those for the other forms. Use `=` only where there can be no
narrowing conversion; for built-in arithmetic types, use `=` only with `auto`.
Avoid `()` initialisation, which allows parsing ambiguities.

```cpp
int x {f(99)};
int y = x;
vector<int> v = {1, 2, 3, 4, 5, 6};
```

The exception is a matter of established convention: for containers there is a
tradition of using `{...}` for a list of elements and `(...)` for sizes, and the
two are not interchangeable.

```cpp
vector<int> v1(10);    // vector of 10 elements with the default value 0
vector<int> v2{10};    // vector of 1 element with the value 10
vector<int> v3(1, 2);  // vector of 1 element with the value 2
vector<int> v4{1, 2};  // vector of 2 element with the values 1 and 2
```

`{}`-initialisers do not allow narrowing conversions, which is usually a good
thing, and they do allow explicit constructors, which is fine because a new
variable is being deliberately initialised.

```cpp
int x {7.9};   // error: narrowing
int y = 7.9;   // OK: y becomes 7. Hope for a compiler warning
int z = gsl::narrow_cast<int>(7.9);  // OK: you asked for it
```

`{}` initialisation can be used for nearly all initialisation, where other forms
cannot:

```cpp
auto p = new vector<int> {1, 2, 3, 4, 5};   // initialized vector
D::D(int a, int b) :m{a, b} {   // member initializer (e.g., m might be a pair)
    // ...
};
X var {};   // initialize var to be empty
struct S {
    int m {7};   // default initializer for a member
    // ...
};
```

That generality is why `{}`-initialisation is often called uniform
initialisation, though a few irregularities remain. One of them concerns `auto`:
initialisation of a variable declared `auto` with a single value had surprising
results until C++17, and the C++17 rules are only somewhat less surprising.

```cpp
auto x1 {7};        // x1 is an int with the value 7
auto x2 = {7};      // x2 is an initializer_list<int> with an element 7
auto x11 {7, 8};    // error: two initializers
auto x22 = {7, 8};  // x22 is an initializer_list<int> with elements 7 and 8
```

Use `={...}` when an `initializer_list<T>` is genuinely wanted:

```cpp
auto fib10 = {1, 1, 2, 3, 5, 8, 13, 21, 34, 55};   // fib10 is a list
```

Another irregularity: `={}` is copy initialisation whereas `{}` is direct
initialisation, and, as with that distinction generally, this leads to
surprises. `{}` accepts explicit constructors; `={}` does not.

```cpp
struct Z { explicit Z() {} };
Z z1{};     // OK: direct initialization, so we use explicit constructor
Z z2 = {};  // error: copy initialization, so we cannot use the explicit constructor
```

Use plain `{}`-initialisation unless disabling explicit constructors is
specifically what is wanted. Finally, `{}` avoids the most vexing parse:

```cpp
template<typename T>
void f()
{
    T x1(1);    // T initialized with 1
    T x0();     // bad: function declaration (often a mistake)
    T y1 {1};   // T initialized with 1
    T y0 {};    // default initialized T
    // ...
}
```

## Decision

Initialise with braces by default.

- Write `T x {expr};` in preference to `T x = expr;` and to `T x(expr);`.
- Never write `T x();` intending a default-constructed object; write `T x {};`.
- Use `=` only where no narrowing conversion is possible, and for built-in
  arithmetic types only in combination with `auto`.
- Use the container convention deliberately: `(...)` when the argument is a size
  or a count, `{...}` when the arguments are elements. Do not treat these as
  interchangeable.
- Use `= {...}` only when an `initializer_list<T>` is what is wanted.
- Where a narrowing conversion is intended, say so with a named narrowing cast
  rather than falling back to `=` to make the compiler quiet.

## Rationale

Brace initialisation is the only form that refuses a narrowing conversion, and
narrowing is the failure this section returns to repeatedly: `int x {7.9};` is a
compile error where `int x = 7.9;` is a silent truncation. That alone would
justify the preference. The most-vexing-parse case is the second argument — `T
x0();` declares a function and is a mistake with no useful interpretation, and
braces make it unwriteable.

## Consequences

### Positive

- Narrowing conversions become compile errors instead of silent truncations.
- The most vexing parse cannot occur, so a default-constructed object always
  looks like one.
- One syntax covers locals, members, default member initialisers, base and
  member initialiser lists, and `new` expressions.

### Negative

- The `initializer_list` constructor wins over every other constructor when one
  is viable, which is why `vector<int> v{10}` is one element and not ten. That
  is a genuine trap the rule can only warn about, not remove.
- The `auto x{1}` versus `auto x = {1}` distinction is a real irregularity, and
  its meaning changed between C++11 and C++17, so older code and older habits
  mean something different.
- `{}` versus `={}` differ in whether explicit constructors are usable, which is
  a subtlety with no visual weight at all.
- The rule has enough exceptions — sizes, `initializer_list`, `auto` — that it
  is a default rather than a rule, and applying it mechanically produces bugs.

### Neutral

- The container size-versus-elements convention is explicitly excepted; it is a
  convention worth knowing rather than a defect to be fixed.
- Nothing here says anything about whether the initialiser should exist; that is
  [0183](0183-es-20-always-initialize-an-object.md).

## Enforcement

- Flag uses of `=` to initialise arithmetic types where narrowing occurs.
- Flag uses of `()` initialisation syntax that are actually declarations. Many
  compilers already warn about this.

## Related

- [0183](0183-es-20-always-initialize-an-object.md) — this record says how; that
  one says that.
- [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md) —
  the `auto` interaction belongs to both.
- [0190](0190-es-27-use-std-array-or-stack-array-for-arrays-on-the-stack.md) —
  `std::array` is one of the types whose initialisation this shapes.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — a
  narrowing conversion refused at compile time is the point of the rule.
