# 0313. Con.3: By default, pass pointers and references to consts

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Con (Constants and immutability), transcribed 2026-08-10

## Context

The point is to avoid a called function unexpectedly changing a value. It is far
easier to reason about a program when called functions do not modify state.

```cpp
void f(char* p);        // does f modify *p? (assume it does)
void g(const char* p);  // g does not modify *p
```

The two declarations differ in what a reader has to do. `g` can be understood
from its signature; `f` can only be understood by reading its body, and by
reading it again after every change to it, because the signature grants
permission that the implementation may start using at any time without breaking
a single caller.

It is not inherently bad to pass a pointer or reference to non-`const`. It is a
statement, and the statement should be true: pass to non-`const` when the called
function is supposed to modify the object, which is the in-out case covered by
[0449](0449-f-17-for-in-out-parameters-pass-by-reference-to-non-const.md). The
complement of this rule is [0207](0207-es-50-dont-cast-away-const.md): `const`
must not be cast away in order to circumvent it. A function that takes a pointer
or reference to `const` and then modifies the object through a cast has
undefined behaviour if the object was itself declared `const`, and has misled
every caller either way.

## Decision

Point and refer to `const` by default in parameter lists.

- Take a parameter as `const T&` or `const T*` whenever the function only reads
  what it refers to.
- Reserve `T&` and `T*` for parameters the function is meant to modify, and read
  their appearance in a signature as a statement that it does.
- Apply the same default to the parameters of the functions being called
  underneath, so that the `const` is not blocked by a callee that never needed
  the permission it asked for.
- Do not use `const_cast` to defeat the promise; where a non-`const`-correct
  library forces one, confine it to a single wrapper as described in
  [0312](0312-con-2-by-default-make-member-functions-const.md).
- Combine this with
  [0448](0448-f-16-for-in-parameters-pass-cheaply-copied-types-by-value-and-others-by-reference-to-const.md):
  the rule is about what a reference or pointer parameter refers to, not an
  instruction to pass small values by reference.

## Rationale

A parameter type is the only part of a function that its callers are guaranteed
to read, and `const` there is the cheapest true statement available. Its value
is asymmetric with the cost of getting it wrong: adding `const` costs six
characters and constrains only the implementation, whereas omitting it silently
transfers a proof obligation to every caller, forever, and can never be
recovered by inspection because a later edit may begin using the permission
without any warning.

## Consequences

### Positive

- A signature answers the question of whether an argument can change, without
  reading the body.
- Callers can pass objects they hold by `const` reference, which is what keeps
  immutability from being lost one layer at a time.
- The compiler rejects modification inside the function rather than leaving it
  to a code review.
- A `const T&` parameter binds to temporaries, so callers are not forced to name
  an intermediate value.

### Negative

- The promise is shallow. `const T&` says nothing about what `T`'s pointer
  members refer to, nor about a `mutable` member, so an object can change during
  a call that took every argument by reference to `const`.
- It constrains only the parameter, not the function: a callee may modify the
  same object through a global, a captured reference, or a second parameter that
  aliases the first.
- `const_cast` remains available and compiles silently, so the guarantee is a
  convention backed by the compiler in the common case rather than a proof.
- Binding a temporary is also a trap: a `const std::string&` parameter accepts a
  `const char*` argument by constructing a temporary `std::string` at every
  call, and the conversion is invisible at the call site.
- The rule does not survive a C ABI boundary intact, and a non-`const`-correct
  dependency propagates its permissiveness upward until someone writes a
  wrapper.

### Neutral

- Optimisation is not the argument. A `const` reference parameter tells the
  compiler little it can rely on, because the referent may be modified through
  another path.
- Where a range is being passed, the same default is spelled `span<const T>`
  rather than `const span<T>&`.

## Enforcement

- Flag a function that does not modify an object passed by pointer or reference
  to non-`const`.
- Flag a function that modifies, using a cast, an object passed by pointer or
  reference to `const`.

## Related

- [0448](0448-f-16-for-in-parameters-pass-cheaply-copied-types-by-value-and-others-by-reference-to-const.md)
  — how to choose between by value and by reference to `const` for an input.
- [0449](0449-f-17-for-in-out-parameters-pass-by-reference-to-non-const.md) — the
  case where a non-`const` reference is the correct statement.
- [0312](0312-con-2-by-default-make-member-functions-const.md) — a class must be
  `const`-correct for callers to be able to follow this rule.
- [0311](0311-con-1-by-default-make-objects-immutable.md) — the same default
  applied to the objects that end up being passed.
