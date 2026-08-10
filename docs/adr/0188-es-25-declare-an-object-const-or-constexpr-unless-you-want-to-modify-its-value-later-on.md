# 0188. ES.25: Declare an object const or constexpr unless you want to modify its value later on

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Two reasons: that way the value cannot be changed by mistake, and that way the
compiler may be offered optimisation opportunities it would not otherwise have.

```cpp
void f(int n)
{
    const int bufmax = 2 * n + 2;  // good: we can't change bufmax by accident
    int xmax = n;                  // suspicious: is xmax intended to change?
    // ...
}
```

The important word in the comment on `xmax` is "suspicious". A non-`const`
declaration is a statement that the value is expected to change, and when it
does not, the reader has to establish that fact by reading the rest of the
function. `const` states it once, at the declaration, and has the compiler check
it.

`constexpr` says more again: not only that the value does not change, but that
it is known during translation, and can therefore be used where a constant
expression is required.

## Decision

Make `const` the default for a declaration and drop it only when mutation is
intended.

- Declare a local `const` when it is assigned once, which after
  [0185](0185-es-22-dont-declare-a-variable-until-you-have-a-value-to-initialize-it-with.md)
  is most locals.
- Declare it `constexpr` when the initialiser is a constant expression and the
  value is wanted at compile time.
- Declare reference and pointer parameters `const` when the function does not
  modify what they refer to.
- Where a value requires several statements to compute and should then be
  immutable, compute it in an immediately invoked lambda and bind the result to
  a `const`; see
  [0191](0191-es-28-use-lambdas-for-complex-initialization-especially-of-const-variables.md).
- Do not remove `const` to make an unrelated edit compile; that is a signal the
  edit is changing something it should not.

## Rationale

`const` is unusual among these rules in that it is checked rather than merely
observed: the compiler rejects the mistake it prevents, at no cost. Its main
value is not the accident it stops but the reading it enables — in a function
where the immutable values are marked, the remaining mutable ones are the state,
and the state is what a reader has to track. The optimisation argument is real
but secondary, and is weaker than commonly claimed, since a compiler can often
prove local immutability without the keyword.

## Consequences

### Positive

- Accidental modification becomes a compile error, not a behaviour change.
- The mutable variables in a function stand out, which is exactly the set a
  reader needs to hold in mind.
- `constexpr` moves computation and checking to compile time, which is the
  preference stated in
  [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md).

### Negative

- `const` on a local disables move-out: returning or passing a `const` local
  copies where a non-`const` one would have moved, which is a silent performance
  cost.
- `const` members make a class non-assignable and effectively non-movable, so
  the keyword does not scale from locals to data members without consequence.
- Applying `const` everywhere adds visual noise to declarations that were
  already stating a type and a name.
- Enforcement is one-sided: a tool can see that a variable was never mutated,
  but not whether the author intended it to be mutable, so every suggestion
  needs a human decision.

### Neutral

- `constexpr` is a stronger claim than `const` and is not always available;
  using `const` where `constexpr` will not compile is not a failure here.
- The rule concerns objects. Whether member functions should be `const` is a
  related but separate matter.

## Enforcement

Look to see whether a variable is actually mutated, and flag it if it is not.
Unfortunately, it may be impossible to distinguish a variable that was not
intended to vary from one that merely did not vary in this version of the code.

## Related

- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — the philosophy
  rule this one applies at the declaration.
- [0191](0191-es-28-use-lambdas-for-complex-initialization-especially-of-const-variables.md)
  — how to keep `const` when the value takes work to compute.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — what
  `constexpr` buys over `const`.
- [0183](0183-es-20-always-initialize-an-object.md) — an object initialised at
  its declaration is one that can usually be `const`.
