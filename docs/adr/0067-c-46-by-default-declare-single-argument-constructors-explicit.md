# 0067. C.46: By default, declare single-argument constructors explicit

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A non-`explicit` single-argument constructor defines an implicit conversion, and
implicit conversions fire where nobody asked for them:

```cpp
class String {
public:
    String(int);   // BAD
    // ...
};
String s = 10;   // surprise: string of size 10
```

The exception is the case where the conversion is the point. If you really want
an implicit conversion from the constructor argument type to the class type,
do not use `explicit`:

```cpp
class Complex {
public:
    Complex(double d);   // OK: we want a conversion from d to {d, 0}
    // ...
};
Complex z = 10.7;   // unsurprising conversion
```

Copy and move constructors should not be made `explicit`, because they do not
perform conversions. Explicit copy and move constructors make passing and
returning by value difficult.

## Decision

Declare every single-argument constructor `explicit` unless the implicit
conversion it enables is deliberate.

- Write `explicit` on any constructor callable with one argument, including one
  with further parameters that all have defaults.
- Omit `explicit` only when the argument type and the class type denote the same
  value — a real number becoming a complex number — and say so in a comment at
  the declaration.
- Never write `explicit` on a copy or move constructor.
- Where the argument is a unit, a size, or an identifier rather than the value
  itself, prefer a named factory function or a strong argument type over an
  implicit conversion.

## Rationale

The `String s = 10;` example is not a contrived one: the failure mode is that
the code compiles, runs, and produces a plausible-looking wrong object, with no
diagnostic anywhere. An implicit conversion is a permission granted to every
call site in the program at once, which is a large thing to hand out as a side
effect of forgetting a keyword.

`explicit` costs one word at the declaration and nothing at any correct call
site, because a call site that meant the conversion can still write it.

## Consequences

### Positive

- Accidental conversions from an unrelated arithmetic or pointer type stop
  compiling.
- Overload resolution has fewer candidates to consider, so ambiguities and
  surprising picks become rarer.
- The declaration states whether the type is a wrapper around its argument or a
  different thing built from it.

### Negative

- Every deliberate conversion becomes more verbose at the call site, and code
  that used brace-elision or aggregate-style initialisation may need rewriting.
- The rule has to be applied by hand to constructors that are single-argument
  only because their later parameters have defaults, which is easy to miss.
- Genuinely value-preserving conversions are penalised alongside the dangerous
  ones, and deciding which is which is a judgement no tool can make.

### Neutral

- Good single-argument non-`explicit` constructors are rare in most codebases,
  so the exception list should stay short and be maintained deliberately.
- `explicit` on a multi-argument constructor is a separate question this record
  does not decide.

## Enforcement

- (Simple) Single-argument constructors should be declared `explicit`. Good
  single-argument non-explicit constructors are rare in most codebases; warn for
  all that are not on a positive list.

## Related

- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — an implicit
  conversion is exactly the weakening of typing that record rejects.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — the
  conversion is type-safe in the language's sense and still wrong in the
  program's sense.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) —
  `explicit` moves a whole class of mistakes to compile time.
- [0071](0071-c-50-use-a-factory-function-if-you-need-virtual-behavior-during-initialization.md)
  — a named factory is the alternative when a constructor argument does not
  really name the value.
