# 0229. ES.87: Don't add redundant == or != to conditions

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Dropping the redundant comparison avoids verbosity, eliminates some
opportunities for mistakes, and keeps style consistent and conventional.

By definition, a condition in an if-statement, while-statement or for-statement
selects between `true` and `false`. A numeric value is compared to `0` and a
pointer value to `nullptr`.

```cpp
// These all mean "if p is not nullptr"
if (p) { ... }            // good
if (p != 0) { ... }       // redundant !=0, bad: don't use 0 for pointers
if (p != nullptr) { ... } // redundant !=nullptr, not recommended
```

Often, `if (p)` is read as "if p is valid", which is a direct expression of the
programmer's intent, whereas `if (p != nullptr)` is a long-winded workaround.
The opposite condition is most easily expressed using a negation:

```cpp
// These all mean "if p is nullptr"
if (!p) { ... }           // good
if (p == 0) { ... }       // redundant == 0, bad: don't use 0 for pointers
if (p == nullptr) { ... } // redundant == nullptr, not recommended
```

The rule is especially useful when a declaration is used as a condition:

```cpp
if (auto pc = dynamic_cast<Circle>(ps)) { ... } // execute if ps points to a kind of Circle, good
if (auto pc = dynamic_cast<Circle>(ps); pc != nullptr) { ... } // not recommended
```

Implicit conversions to `bool` are applied in conditions, which is what makes
the stream idiom work:

```cpp
for (string s; cin >> s; ) v.push_back(s);
```

This invokes `istream`'s `operator bool()`.

Explicit comparison of an integer to `0` is in general not redundant. Unlike
pointers and Booleans, an integer often has more than two reasonable values, and
`0` is often used to indicate success, so it is best to be specific:

```cpp
void f(int i)
{
    if (i)            // suspect
    // ...
    if (i == success) // possibly better
    // ...
}
```

It has been noted that

```cpp
if(strcmp(p1, p2)) { ... }   // are the two C-style strings equal? (mistake!)
```

is a common beginner's error. If you use C-style strings you must know the
`<cstring>` functions well; being verbose and writing

```cpp
if(strcmp(p1, p2) != 0) { ... }   // are the two C-style strings equal? (mistake!)
```

would not in itself save you.

## Decision

Write conditions that rely on the value's own truthiness where the value has
only two meaningful states.

- Write `if (p)` and `if (!p)` for pointers and smart pointers, not
  `p != nullptr` or `p == nullptr`.
- Never compare a pointer to the literal `0`.
- Write `if (b)`, not `if (b == true)`, for a `bool`.
- Use a declaration as a condition — `if (auto pc = dynamic_cast<Circle*>(ps))`
  — rather than declaring and then comparing.
- Keep the explicit comparison for integers, where `0` is one value among many
  and often means success: write `if (i == success)`, not `if (i)`.
- Do not treat the shorter form as a substitute for knowing what a function
  returns; `strcmp` returns an ordering, not a Boolean.

## Rationale

The argument is not brevity for its own sake. `if (p)` is read as "if p is
valid", which is what the code means; `if (p != nullptr)` asks the reader to
perform the comparison mentally and arrive at the same conclusion. Every
redundant term is a term that can be written the wrong way round.

The integer exception is what keeps the rule honest, and it is the part most
often lost. The reason pointers and Booleans may be tested directly is that they
have exactly two meaningful states. An `int` does not, so `if (i)` says
"non-zero" when the author almost always meant something narrower, and the
`strcmp` example shows how expensive that confusion is.

## Consequences

### Positive

- Conditions read as statements of intent rather than as comparisons to be
  evaluated.
- Removes a place where `==` can be written for `!=`, or the operands
  transposed.
- Makes the declaration-as-condition form natural, which narrows the scope of
  the declared variable to the branch that uses it.

### Negative

- Reasonable people find `p != nullptr` clearer, and the guidelines themselves
  only rate it "not recommended" rather than bad, so the rule spends review
  effort on a difference of taste.
- Relying on implicit conversion to `bool` is exactly what `explicit operator
  bool` was introduced to constrain, and the short form obscures which
  conversion is being invoked — a real cost for types where truthiness means
  something non-obvious, such as a stream's failure state.
- The integer carve-out means the rule cannot be applied mechanically: a tool
  must know whether the operand is a pointer, a Boolean, or an integer, and the
  author must know what `0` means for that particular value.
- For a type with both `operator bool` and a comparison, the short form can
  select a different overload than the reader expects.

### Neutral

- Which spelling of the null pointer to use is settled independently: it is
  `nullptr`, never `0` or `NULL`, on the rare occasions the comparison is
  written out.
- The rule is about conditions in control-flow statements, not about
  comparisons appearing as ordinary values.

## Enforcement

Easy — just check for redundant use of `!=` and `==` in conditions.

## Related

- [0009](0009-p-3-express-intent.md) — `if (p)` states the intent that the
  comparison merely implies.
- [0199](0199-es-42-keep-use-of-pointers-simple-and-straightforward.md) — the
  pointers this rule tests.
- [0176](0176-es-6-declare-names-in-for-statement-initializers-and-conditions-to-limit-scope.md)
  — the declaration-as-condition form this rule makes idiomatic.
- [0197](0197-es-40-avoid-complicated-expressions.md) — removing redundant terms
  is the smallest case of simplifying an expression.
