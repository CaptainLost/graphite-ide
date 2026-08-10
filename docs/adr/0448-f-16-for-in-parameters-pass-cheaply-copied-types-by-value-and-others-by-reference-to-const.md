# 0448. F.16: For "in" parameters, pass cheaply-copied types by value and others by reference to const

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Both forms let the caller know that the function will not modify the argument,
and both allow initialisation by rvalues.

What is "cheap to copy" depends on the machine architecture, but two or three
words — doubles, pointers, references — are usually best passed by value. When
copying is cheap, nothing beats the simplicity and safety of copying, and for
small objects it is also faster than passing by reference, because access does
not require an extra indirection inside the function.

```cpp
void f1(const string& s);  // OK: pass by reference to const; always cheap
void f2(string s);         // bad: potentially expensive
void f3(int x);            // OK: Unbeatable
void f4(const int& x);     // bad: overhead on access in f4()
```

```cpp
int multiply(int, int); // just input ints, pass by value
// suffix is input-only but not as cheap as an int, pass by const&
string& concatenate(string&, const string& suffix);
void sink(unique_ptr<widget>);  // input only, and moves ownership of the widget
```

For advanced uses only, where the rvalue case really does need optimising: if
the function will unconditionally move from the argument, take it by `&&`; if
the function will keep a copy of the argument, add an rvalue overload alongside
the `const&` one and `std::move` in its body; and in special cases, such as
several "input plus copy" parameters at once, consider perfect forwarding.

Avoid esoteric techniques such as passing arguments as `T&&` "for efficiency".
Most rumours about performance advantages from passing by `&&` are false or
brittle.

A reference can be assumed to refer to a valid object; this is a language rule,
and there is no legitimate "null reference". Where the notion of an optional
value is needed, use a pointer, `std::optional`, or a special value denoting "no
value".

## Decision

Pass an input parameter by value when it is cheap to copy, and by reference to
`const` otherwise.

- Pass by value anything up to about two or three words: the built-in types,
  small handles, pointers, iterators, small trivially copyable structs.
- Pass by reference to `const` anything larger, and anything whose copy
  allocates: strings, containers, matrices, non-trivial user-defined types.
- Take the pass-by-value form when the function takes ownership of a move-only
  type, as with `void sink(unique_ptr<widget>)`.
- Do not pass an input parameter by `T&&` for speed; use `&&` only for the
  will-move-from case, and only after showing the need.
- Where an input parameter is genuinely optional, use a pointer or
  `std::optional`, not a reference that some callers might not have an object
  for.

## Rationale

The threshold cuts both ways, and that is the part that is usually forgotten.
`void f4(const int& x)` is as much a mistake as `void f2(string s)`: the
reference costs an indirection on every access inside the body and buys nothing,
because the copy it avoided was a register move. Passing by value below the
threshold is not merely acceptable but faster, and it removes an aliasing
possibility the optimiser would otherwise have to assume.

## Consequences

### Positive

- Both forms tell the caller the argument will not be modified, and both accept
  temporaries, so the interface is uniform from the outside.
- Small parameters stay in registers and are accessed without indirection.
- Large parameters are not copied, which matters most for the types whose copies
  allocate.
- The rule is mechanical enough for a tool to check, using `sizeof` against the
  pointer size.

### Negative

- "Cheap to copy" is architecture-dependent, so the correct choice for a type
  sitting near the threshold differs between targets and the rule cannot be
  applied once and for all.
- Size is the wrong proxy for some types: a small object with an expensive copy
  constructor passes the size test and fails the intent of the rule.
- Passing by `const&` binds to a temporary whose lifetime ends at the full
  expression, so a callee that stores the reference has a dangling one, and the
  signature gives no warning.
- Changing a parameter from `const&` to by-value or back is an ABI change, so
  the rule cannot be applied retroactively at a stable boundary.

### Neutral

- `sink(unique_ptr<widget>)` is by value for ownership reasons, not because a
  `unique_ptr` is small; the two justifications happen to agree.
- The rule is about "in" parameters only; in-out and out parameters are governed
  by their own records.

## Enforcement

- (Simple) Warn when a parameter passed by value has a size greater than
  `2 * sizeof(void*)`. Suggest a reference to `const` instead.
- (Simple) Warn when a parameter passed by reference to `const` has a size less
  than `2 * sizeof(void*)`. Suggest passing by value instead.
- (Simple) Warn when a parameter passed by reference to `const` is moved.

## Related

- [0447](0447-f-15-prefer-simple-and-conventional-ways-of-passing-information.md)
  — this is the normal technique that record defers to.
- [0449](0449-f-17-for-in-out-parameters-pass-by-reference-to-non-const.md) — the
  in-out case, and why `const` is what distinguishes the two.
- [0450](0450-f-18-for-will-move-from-parameters-pass-by-x-and-std-move-the-parameter.md)
  — the advanced alternative for rvalue arguments.
- [0454](0454-f-60-prefer-t-over-t-when-no-argument-is-a-valid-option.md) — what
  to do when the input is optional.
