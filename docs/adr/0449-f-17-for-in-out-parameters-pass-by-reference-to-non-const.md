# 0449. F.17: For "in-out" parameters, pass by reference to non-const

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Passing by reference to non-`const` makes it clear to callers that the object is
assumed to be modified.

```cpp
void update(Record& r);  // assume that update writes to r
```

A `T&` argument can pass information into a function as well as out of it, which
is what makes it an in-out parameter — and that is in itself a problem and a
source of errors:

```cpp
void f(string& s)
{
    s = "New York";  // non-obvious error
}

void g()
{
    string buffer = ".................................";
    f(buffer);
    // ...
}
```

Here the writer of `g()` is supplying a buffer for `f()` to fill, but `f()`
simply replaces it, at a somewhat higher cost than a simple copy of the
characters. A bad logic error can follow if the writer of `g()` incorrectly
assumes the size of the buffer afterwards.

## Decision

Pass a parameter the function both reads and writes by reference to
non-`const`.

- Use `T&` exactly when the incoming value matters and the outgoing value
  matters: updating a record, appending to a buffer, advancing a stream.
- Do not use `T&` for a parameter the function only writes; that is an output
  and belongs in the return value.
- Do not use `T&` for a parameter the function only reads; that is an input and
  takes `const&` or by value.
- State in the function's documentation whether an in-out parameter is extended,
  overwritten, or reset, since the signature cannot distinguish them.
- Do not move from a non-`const` reference parameter: the caller still owns the
  object and expects it to be usable.

## Rationale

`T&` is the only one of the three passing forms whose meaning the signature does
not fully determine, and the `string&` example is what that costs. The type says
"this may change" and nothing more, so "fill the buffer I gave you" and "replace
whatever was there" are indistinguishable to the caller, and the two are only
compatible by accident. Restricting `T&` to the genuinely in-out case at least
narrows the ambiguity to one axis, since the reader then knows the prior value
was meant to be read.

## Consequences

### Positive

- The caller can see from the signature alone that the argument may be modified.
- No copy is made, so an in-out parameter carrying capacity — a `string` or a
  `vector` reused across calls — avoids repeated allocation.
- Restricting `T&` to in-out makes a non-`const` reference informative wherever
  it appears.

### Negative

- The signature still cannot say what kind of modification occurs, so "fill" and
  "replace" look identical and the disagreement in the example remains
  expressible.
- The rule makes the compiler's help conditional on the author choosing
  correctly; nothing prevents a `T&` parameter that is only ever written.
- References cannot express "no argument", so an optional in-out parameter needs
  a pointer instead and the passing scheme becomes inconsistent.
- Mutating through a parameter is at odds with preferring pure functions, and
  this record legitimises it for the cases where it is the right answer.

### Neutral

- The caller-allocated out pattern deliberately uses this form even though the
  incoming value is only a capacity, and the guidelines call it an in-out
  parameter for exactly that reason.
- The rule does not say when a function ought to have an in-out parameter, only
  how one is spelled.

## Enforcement

- (Moderate) Warn about functions taking a reference to non-`const` parameter
  that do not write to it.
- (Simple) Warn when a non-`const` parameter passed by reference is moved from.

## Related

- [0448](0448-f-16-for-in-parameters-pass-cheaply-copied-types-by-value-and-others-by-reference-to-const.md)
  — the input case, distinguished from this one by `const`.
- [0452](0452-f-20-for-out-output-values-prefer-return-values-to-output-parameters.md)
  — why a write-only `T&` is the wrong shape.
- [0447](0447-f-15-prefer-simple-and-conventional-ways-of-passing-information.md)
  — the umbrella rule this is one of the three normal cases of.
- [0020](0020-i-1-make-interfaces-explicit.md) — the ambiguity noted above is a
  place where the interface fails to be explicit on its own.
