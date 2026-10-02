# 0127. C.162: Overload operations that are roughly equivalent

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Having different names for logically equivalent operations on different argument
types is confusing, leads to encoding type information in function names, and
inhibits generic programming.

Consider:

```cpp
void print(int a);
void print(int a, int base);
void print(const string&);
```

These three functions all print their arguments, appropriately for what each was
given. Conversely:

```cpp
void print_int(int a);
void print_based(int a, int base);
void print_string(const string&);
```

These do exactly the same three things, but the name now carries the argument
type. Adding to the name introduced verbosity and inhibits generic code: a
template that wants to print whatever it was given can call `print(x)` and let
overload resolution do the work, but it cannot assemble `print_int` or
`print_string` from the type of `x`.

This rule is one half of a pair. Its counterpart, C.163, forbids the opposite
mistake — giving the same name to operations that are not equivalent. Together
they say that a name should track the operation and nothing else.

## Decision

Give logically equivalent operations the same name, and let the argument types
distinguish them.

- Overload one name across the types it applies to rather than appending a type
  to each name.
- Do not encode a parameter's type, count or units in a function name where the
  signature already states it.
- Treat operations as equivalent when a caller who knows what the name means
  needs to know nothing else to predict what each overload does.
- Where an overload set is meant to be extended by users, make the extension
  point explicit rather than relying on a naming convention.
- Where the operations turn out not to be equivalent, do not force them into one
  name; that is the case
  [0128](0128-c-163-overload-only-for-operations-that-are-roughly-equivalent.md)
  governs.

## Rationale

The decisive argument is not brevity but reach. Type-decorated names are a
manual implementation of overload resolution, performed by the person writing
the call, and a template has no person available — it cannot spell the name it
needs. A single overloaded name is therefore the difference between an operation
that generic code can use and one that must be adapted for every caller.

## Consequences

### Positive

- Generic code can call the operation without knowing the argument's type.
- Callers learn one name and get the whole family, instead of a vocabulary.
- Adding support for a new type adds an overload rather than a new name that
  every caller must be taught.

### Negative

- Overload resolution is genuinely hard to predict, and a large set with
  implicit conversions can select a surprising candidate that a distinct name
  never would have.
- Compiler diagnostics degrade: a failure to match names every candidate, which
  is far less legible than one signature and one error.
- Grepping for a distinct name finds every call; grepping for an overloaded one
  finds all of them mixed together.
- Adding an overload can silently change which function existing call sites
  resolve to, which a new distinct name never does.

### Neutral

- The rule concerns naming, not whether the operations should exist.
- "Roughly equivalent" is a judgement about meaning, which is why upstream
  offers no enforcement for either half of the pair.

## Related

- [0128](0128-c-163-overload-only-for-operations-that-are-roughly-equivalent.md)
  — the complementary prohibition; the two are meant to be read together.
- [0132](0132-c-167-use-an-operator-for-an-operation-with-its-conventional-meaning.md)
  — the same argument applied to operators, where `<<` is the shared name.
- [0009](0009-p-3-express-intent.md) — a name should say what is done, not what
  it is done to.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — generic code is the beneficiary, and concepts are how its requirements are
  stated.
