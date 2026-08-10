# 0237. ES.107: Don't use unsigned for subscripts, prefer gsl::index

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

To avoid signed/unsigned confusion, to enable better optimisation, to enable
better error detection, and to avoid the pitfalls with `auto` and `int`. Every
obvious spelling of an index loop is wrong in some way:

```cpp
vector<int> vec = /*...*/;

for (int i = 0; i < vec.size(); i += 2)                    // might not be big enough
    cout << vec[i] << '\n';
for (unsigned i = 0; i < vec.size(); i += 2)               // risk wraparound
    cout << vec[i] << '\n';
for (auto i = 0; i < vec.size(); i += 2)                   // might not be big enough
    cout << vec[i] << '\n';
for (vector<int>::size_type i = 0; i < vec.size(); i += 2) // verbose
    cout << vec[i] << '\n';
for (auto i = vec.size()-1; i >= 0; i -= 2)                // bug
    cout << vec[i] << '\n';
for (int i = vec.size()-1; i >= 0; i -= 2)                 // might not be big enough
    cout << vec[i] << '\n';
```

The fifth is the outright bug: `auto` deduces the unsigned `size_type` from
`vec.size()`, so `i >= 0` is always true and the loop never terminates — and on
an empty vector it starts at the largest `size_t`. The `auto i = 0` case is the
converse pitfall: `auto` deduces `int` from the literal, not the type the reader
assumed.

A signed index type wide enough for any container removes all six problems:

```cpp
vector<int> vec = /*...*/;

for (gsl::index i = 0; i < vec.size(); i += 2)             // ok
    cout << vec[i] << '\n';
for (gsl::index i = vec.size()-1; i >= 0; i -= 2)          // ok
    cout << vec[i] << '\n';
```

Note what makes the descending form work. `vec.size()-1` is still evaluated in
the container's unsigned `size_type`, so on an empty vector the subtraction
still wraps to the largest `size_t`. It is the conversion of that value into the
signed `gsl::index` — modular, and well defined since C++20 — that turns it back
into `-1`, at which point `i >= 0` is false and the body never runs. The signed
index rescues the loop after the wrap rather than preventing it.

The built-in array uses signed subscripts; the standard-library containers use
unsigned subscripts. No perfect and fully compatible solution is possible unless
and until the standard-library containers change. Given the known problems with
unsigned and with signed/unsigned mixtures, it is better to stick to signed
integers of a sufficient size, which is what `gsl::index` guarantees. The same
choice applies to containers you write:

```cpp
template<typename T>
struct My_container {
public:
    // ...
    T& operator[](gsl::index i);    // not unsigned
    // ...
};
```

The alternatives for users are to use algorithms, to use range-`for`, or to use
iterators and pointers — all of which remove the index entirely.

## Decision

Index with a signed type wide enough for any container.

- Declare index variables as `gsl::index`, or as a local signed alias of
  `std::ptrdiff_t` where the GSL is not available.
- Do not use `unsigned`, `size_t` or a container's `size_type` as an index
  variable.
- Do not use `auto` for an index initialised from `.size()` or from a literal;
  in both cases the deduced type is the wrong one.
- Declare `operator[]` on your own containers to take a signed index.
- Prefer to have no index at all: use a standard algorithm, a range-`for`, or
  iterators, and reach for an explicit index only when the position itself is
  needed.

## Rationale

The list of six loops is the argument. Each spelling is what an experienced
programmer would write, each is defensible, and each is wrong — either too
narrow for a large container, or unsigned and therefore capable of wrapping, or
verbose enough that nobody uses it, or, in the `auto` descending case, an
infinite loop that compiles without complaint.

The descending loop is what settles it. `i >= 0` is a natural termination
condition, it is meaningless for an unsigned type, and the compiler is
permitted to note that it is always true and delete the check. No amount of care
rescues an unsigned descending index; only a signed type does.

## Consequences

### Positive

- Descending loops terminate, because `size() - 1` on an empty container lands
  in the index as `-1` rather than staying the largest `size_t`.
- One index type covers every container, so the loop does not have to name the
  container's `size_type`.
- A signed index gives the optimiser the no-overflow assumption it cannot make
  for an unsigned one, which is the "better optimisation" the rule cites.

### Negative

- The comparison `i < vec.size()` still mixes signed and unsigned, so the
  recommended loop generates the very warning
  [0230](0230-es-100-dont-mix-signed-and-unsigned-arithmetic.md) asks for; the
  guidelines resolve this only by telling compilers not to warn in that specific
  shape, which is a request, not a mechanism.
- Passing a `gsl::index` to `operator[]` converts back to unsigned at the call,
  so the conversion is not removed, only moved to a point where the value is
  known non-negative.
- `gsl::index` is a GSL type. Adopting the rule as written means taking that
  dependency, or defining a local alias and losing the shared vocabulary.
- Declaring your own `operator[]` as signed makes your containers inconsistent
  with the standard ones, which is a real cost for generic code written against
  both.

### Neutral

- The guidelines' example promising a demonstration of improved code generation
  is an unfilled placeholder, so the optimisation claim is transcribed here
  without the supporting figures.
- The best outcome is that no index is written at all; where an algorithm or a
  range-`for` applies, this rule does not arise.

## Enforcement

- Very tricky as long as the standard-library containers get it wrong.
- To avoid noise, do not flag a mixed signed/unsigned comparison where one of
  the arguments is `sizeof` or a call to a container's `.size()` and the other
  is `ptrdiff_t`.

## Related

- [0232](0232-es-102-use-signed-types-for-arithmetic.md) — the general rule this
  applies to subscripts.
- [0230](0230-es-100-dont-mix-signed-and-unsigned-arithmetic.md) — the mixture
  the standard library forces, and the noise exemption both rules share.
- [0234](0234-es-104-dont-underflow.md) — the descending loop that wraps instead
  of going negative.
- [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md) —
  the `auto` whose deduction is the trap in two of these loops.
