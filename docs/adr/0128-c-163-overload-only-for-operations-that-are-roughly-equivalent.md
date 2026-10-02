# 0128. C.163: Overload only for operations that are roughly equivalent

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Having the same name for logically different functions is confusing and leads to
errors when using generic programming.

Consider:

```cpp
void open_gate(Gate& g);   // remove obstacle from garage exit lane
void fopen(const char* name, const char* mode);   // open file
```

The two operations are fundamentally different, and unrelated, so it is good
that their names differ. Conversely:

```cpp
void open(Gate& g);   // remove obstacle from garage exit lane
void open(const char* name, const char* mode ="r");   // open file
```

The two operations are still fundamentally different and unrelated, but the
names have been reduced to their common minimum, opening opportunities for
confusion. Fortunately, the type system will catch many such mistakes.

Be particularly careful about common and popular names, such as `open`, `move`,
`+` and `==`.

This rule is the counterpart of C.162, which requires that equivalent operations
share a name. The two together say the name should follow the operation: same
operation, same name; different operation, different name.

## Decision

Put two functions in the same overload set only when they do roughly the same
thing.

- Before overloading a name, ask whether a caller who understands one overload
  would predict the behaviour of the other; if not, the names should differ.
- Do not shorten a specific name to a general one merely because the signatures
  happen to differ.
- Be especially wary with short, popular verbs — `open`, `close`, `move`, `set`,
  `get`, `add` — and with the operators `+` and `==`, where the conventional
  meaning is already spoken for.
- Where unrelated operations must live in the same program, separate them by
  namespace or by type rather than relying on overload resolution to keep them
  apart.
- Do not rely on the type system to catch the confusion; it catches many such
  mistakes, not all of them.

## Rationale

The type system is the reason this failure is tolerable and also the reason it
is dangerous. Most collisions of unrelated operations under one name will not
compile, so the ones that survive are precisely those where the argument types
were compatible enough to bind — which means the surviving mistake is a call
that compiles, runs, and does something entirely unrelated to what was intended.
In a template the position is worse still, since the call is written once and
resolved against whatever type turns up.

## Consequences

### Positive

- A name predicts behaviour, so reading a call site does not require resolving
  overloads by hand.
- Unrelated operations stay separately greppable, documentable and changeable.
- Generic code that calls a name gets the operation it meant, rather than
  whatever else happened to answer to it.

### Negative

- "Roughly equivalent" is a judgement with no test behind it, so the rule offers
  no way to settle a disagreement.
- It pulls against
  [0127](0127-c-162-overload-operations-that-are-roughly-equivalent.md): the two
  rules stake out opposite errors, and every naming decision falls somewhere
  between them, with the boundary left to taste.
- Following it can force longer, uglier names on operations that read perfectly
  well in their own context.
- It gives no guidance on operations that are equivalent within one domain and
  unrelated across two, which is the case that actually causes arguments.

### Neutral

- Upstream offers no enforcement for this rule, which follows from its resting
  on meaning rather than form.
- The rule concerns names within one overload set; identical names in different
  namespaces are a separate matter.

## Related

- [0127](0127-c-162-overload-operations-that-are-roughly-equivalent.md) — the
  complementary requirement; neither rule makes sense alone.
- [0125](0125-c-160-define-operators-primarily-to-mimic-conventional-usage.md) —
  the operator form of the same prohibition, with `+` and `==` named in both.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — distinct
  types are what keep an overload set from silently binding the wrong call.
- [0009](0009-p-3-express-intent.md) — a name that covers two intentions
  expresses neither.
