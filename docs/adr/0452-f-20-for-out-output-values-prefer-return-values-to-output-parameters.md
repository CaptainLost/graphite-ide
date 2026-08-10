# 0452. F.20: For "out" output values, prefer return values to output parameters

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

A return value is self-documenting, whereas a `&` could be either in-out or
out-only and is liable to be misused. This includes large objects such as
standard containers, which use implicit move operations for performance and to
avoid explicit memory management. Where there are multiple values to return, a
tuple or a similar multi-member type is the answer.

```cpp
// OK: return pointers to elements with the value x
vector<const int*> find_all(const vector<int>&, int x);
// Bad: place pointers to elements with value x in-out
void find_all(const vector<int>&, vector<const int*>& out, int x);
```

A struct of many individually cheap-to-move elements might nevertheless be
expensive to move in aggregate.

Returning a `const` value is not recommended. That older advice is now obsolete;
it does not add value, and it interferes with move semantics:

```cpp
const vector<int> fct();    // bad: that "const" is more trouble than it is worth

void g(vector<int>& vx)
{
    // ...
    fct() = vx;   // prevented by the "const"
    // ...
    vx = fct(); // expensive copy: move semantics suppressed by the "const"
    // ...
}
```

The argument for adding `const` to a return value is that it prevents a very
rare accidental access to a temporary. The argument against is that it prevents
the very frequent use of move semantics.

There are exceptions. For non-concrete types, such as types in an inheritance
hierarchy, return the object by `unique_ptr` or `shared_ptr`. If a type is
expensive to move, consider allocating it on the free store and returning a
handle, or passing a reference to a non-`const` target object to fill. And to
reuse an object that carries capacity, such as a `std::string` or a
`std::vector`, across multiple calls in an inner loop, treat it as an in-out
parameter and pass it by reference.

Assuming `Matrix` has move operations, possibly by keeping its elements in a
`std::vector`:

```cpp
Matrix operator+(const Matrix& a, const Matrix& b)
{
    Matrix res;
    // ... fill res with the sum ...
    return res;
}

Matrix x = m1 + m2;  // move constructor
y = m3 + m3;         // move assignment
```

The return value optimisation does not handle the assignment case, but move
assignment does.

```cpp
struct Package {      // exceptional case: expensive-to-move object
    char header[16];
    char load[2024 - 16];
};

Package fill();       // Bad: large return value
void fill(Package&);  // OK
int val();            // OK
void val(int&);       // Bad: Is val reading its argument
```

## Decision

Return output values; do not write them through parameters.

- Give the function a return type that is the value it produces, including when
  that value is a container.
- Do not add a reference parameter in order to avoid a copy; move semantics and
  copy elision already handle the ordinary case.
- Do not qualify a return type with `const`.
- Return a `unique_ptr` or `shared_ptr` where the result is polymorphic and a
  value cannot be returned.
- Use a fill-this-object parameter only in the stated exceptional cases: a type
  that is genuinely expensive to move, such as a large array-like POD, or an
  object whose capacity is being reused across calls in an inner loop.
- Where several values come out, return a struct or a tuple rather than adding
  output parameters.

## Rationale

The decisive property is that a return value cannot be misread. A `T&` parameter
occupies the same syntactic slot whether the function reads it, writes it, or
both, so `void val(int&)` leaves the caller genuinely unable to tell whether the
argument's prior value mattered — and the caller must initialise it just in
case. A return value states "produced here, nothing incoming" with no
possibility of a different reading, and since C++11 it costs nothing to say so
for the types that used to make people reach for output parameters.

## Consequences

### Positive

- The direction of every parameter is inferable from its type, because none of
  them are outputs.
- Callers can use the result in an expression, declare it `auto` and `const`,
  and never see an uninitialised variable.
- Move semantics and copy elision make the value form as fast as the parameter
  form for the overwhelming majority of types.
- Dropping `const` from return types restores move assignment at call sites.

### Negative

- The exceptions are not marginal: an expensive-to-move aggregate and a
  capacity-carrying buffer reused in a loop are both common in performance work,
  and the rule then requires the very shape it otherwise forbids.
- Copy elision is guaranteed only for prvalue initialisation; the assignment
  case relies on move assignment, so a type with no move operations pays a real
  copy.
- A function that returns a value cannot easily report failure alongside it
  without a wrapper type, which pushes the design toward the multi-value shapes
  of
  [0453](0453-f-21-to-return-multiple-out-values-prefer-returning-a-struct-or-tuple.md).
- Returning `unique_ptr` for polymorphic results reintroduces allocation the
  value form avoided.

### Neutral

- A single-value return and a `Result`-style wrapper are the same shape from
  this rule's point of view; the wrapper is still a return value.
- The rule concerns "out" values only; genuine in-out parameters are unaffected.

## Enforcement

- Flag reference-to-non-`const` parameters that are not read before being
  written to and are of a type that could be cheaply returned; they should be
  "out" return values.
- Flag returning a `const` value. To fix, remove the `const` and return a
  non-`const` value instead.

## Related

- [0453](0453-f-21-to-return-multiple-out-values-prefer-returning-a-struct-or-tuple.md)
  — what to do when there is more than one output value.
- [0449](0449-f-17-for-in-out-parameters-pass-by-reference-to-non-const.md) — the
  case a `T&` parameter is legitimately for.
- [0439](0439-f-2-a-function-should-perform-a-single-logical-operation.md) —
  multiple output parameters are that record's symptom of a fused function.
