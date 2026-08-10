# 0045. C.7: Don't define a class or enum and declare a variable of its type in the same statement

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Mixing a type definition and the definition of another entity in the same
declaration is confusing and unnecessary. The language permits it, inherited
from C, but nothing is gained by it.

```cpp
struct Data { /*...*/ } data{ /*...*/ };   // bad
```

```cpp
struct Data { /*...*/ };                   // good
Data data{ /*...*/ };
```

The two forms declare exactly the same things. The difference is that in the
first, a reader scanning for the definition of `Data` and a reader scanning for
the definition of `data` are both looking at one statement that answers a
question neither of them asked. The combined form also hides a common error: a
class or enum definition whose closing brace is not followed by a semicolon is
still valid syntax, because whatever follows is read as a declarator.

## Decision

Define the type, end the statement, then declare the variable.

- Follow the closing brace of every class, struct, union or enum definition
  immediately with a semicolon.
- Never place a declarator between that brace and the semicolon.
- Apply this to anonymous types too: if a variable needs a type, give the type a
  name and declare the variable separately.
- The anonymous union member of a class, which the language requires to be
  written without a separate declaration, is outside this rule.

## Rationale

There is no cost to separating the two, so the rule is free, which is rare among
these guidelines. The only argument for the combined form is brevity, and it
saves one line while making a whole class of missing-semicolon errors compile
into something the author did not write.

The diagnostic value is what makes this worth a record rather than a style
preference: once the rule is in force, "a `}` not followed by `;`" is
unambiguously a defect, and a tool can say so.

## Consequences

### Positive

- The missing semicolon after a class or enum definition becomes mechanically
  detectable.
- A type definition and a variable definition are found by separate searches,
  each landing on one statement.
- The type acquires a name that can be referred to later, which the combined
  form often skips.

### Negative

- It costs a line, and for a genuinely single-use anonymous aggregate the extra
  name is noise that has to be invented and then kept meaningful.
- The rule is invisible in the failure case: nothing goes wrong when it is
  broken, so it is enforced only by a checker or by review.
- Existing C-derived headers use the combined form freely, so the rule cannot be
  applied to vendored code and produces noise if a checker is pointed at it.

### Neutral

- The rule concerns declaration syntax only; it says nothing about where a type
  should be defined.
- Nested type definitions inside a class are unaffected, as long as each ends
  with its own semicolon.

## Enforcement

- Flag any case where the `}` of a class or enumeration definition is not
  followed by a `;`. In that position, the semicolon is missing.

## Related

- [0009](0009-p-3-express-intent.md) — one statement should say one thing.
- [0040](0040-c-1-organize-related-data-into-structures-structs-or-classes.md) —
  the types this record asks to be declared cleanly.
