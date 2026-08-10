# 0469. F.51: Where there is a choice, prefer default arguments over overloading

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Default arguments simply provide alternative interfaces to a single
implementation. There is no guarantee that a set of overloaded functions all
implement the same semantics, and using default arguments avoids the code
replication that an overload set invites.

There is a choice between a default argument and overloading when the
alternatives are drawn from a set of arguments of the same types. For example:

```cpp
void print(const string& s, format f = {});
```

as opposed to:

```cpp
void print(const string& s);            // use default format
void print(const string& s, format f);
```

There is not a choice when a set of functions is used to do a semantically
equivalent operation on a set of types. For example:

```cpp
void print(const char&);
void print(int);
void print(zstring);
```

Here no default argument could express the difference, because the parameters
differ in type rather than in presence.

## Decision

Where the alternatives differ only in how many arguments of the same types are
supplied, use a default argument rather than an overload.

- Give the trailing parameter a default and write one function, when the shorter
  form means "the same operation with a standard value here".
- Keep overloads for the case they are for: the same operation over different
  types.
- Do not write an overload whose body forwards to another overload with a
  constant filled in. That is a default argument spelled at length.
- Order parameters so that the ones with defaults come last and the defaulted
  tail reads as optional detail, and keep the number of them small, per
  [0034](0034-i-23-keep-the-number-of-function-arguments-low.md).
- Do not give a default argument to a virtual function; the default is chosen by
  the static type of the expression and the body by the dynamic type, so the two
  can disagree.

## Rationale

The decisive point is that a default argument is a guarantee and an overload is
not. `print(s)` and `print(s, f)` reaching the same body cannot drift apart;
two separately written overloads can, and typically do, once one of them is
maintained and the other is not.

The replication argument follows from that. Every forwarding overload is a copy
of a signature that must be kept in step with its sibling, for no benefit that
a defaulted parameter does not already provide.

## Consequences

### Positive

- One implementation means one place for the semantics to live, so the short and
  long forms cannot diverge.
- The overload set stays small, which keeps overload resolution and its error
  messages comprehensible.
- The default value is visible in the declaration, so a reader sees what the
  short form actually does.

### Negative

- A default argument is part of the declaration, not the definition, so changing
  it silently changes every call site's behaviour on recompilation, and callers
  in another translation unit compiled against an older header disagree about
  it. An overload does not have this problem.
- Defaults cannot be overridden meaningfully in a derived class, so the rule has
  to carve out virtual functions, and that carve-out is the case most likely to
  come up in a class hierarchy.
- A default of `{}` requires the parameter type to be default-constructible and
  to have a sensible default, which is a constraint on the type imposed by a
  choice about the function.
- Defaulted parameters make it easy to accumulate a long tail of optional
  arguments, so the rule can push an interface towards exactly the shape
  [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md)
  warns about.

### Neutral

- The rule applies only where there genuinely is a choice; a type-dispatched
  overload set is not in scope and is not improved by defaults.
- Whether a defaulted parameter or a small options struct is better is a further
  question this record does not settle.

## Enforcement

- Warn on an overload set where the overloads have a common prefix of
  parameters, for example `f(int)`, `f(int, const string&)`,
  `f(int, const string&, double)`. This enforcement should be reviewed if it
  proves too noisy in practice.

## Related

- [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) — defaults make
  a long parameter list easy to write; the count still matters.
- [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md)
  — a defaulted tail of same-typed parameters is an argument-order trap.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the
  type-dispatched overload set this rule deliberately leaves alone.
- [0468](0468-f-50-use-a-lambda-when-a-function-wont-do-to-capture-local-variables-or-to-write-a-local-function.md)
  — the other rule that turns on whether overloading is required.
