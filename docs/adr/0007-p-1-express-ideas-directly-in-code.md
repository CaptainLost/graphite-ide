# 0007. P.1: Express ideas directly in code

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The first of the philosophy rules, and the one the rest are derived from. It
bears on every header in Core, where the signature is the whole contract a
caller sees.

## Decision

Adopt P.1 as written. Transcribed in full:

> **Reason.** Compilers don't read comments (or design documents) and neither do
> many programmers (consistently). What is expressed in code has defined
> semantics and can (in principle) be checked by compilers and other tools.

**Example.**

```cpp
class Date {
public:
    Month month() const;  // do
    int month();          // don't
    // ...
};
```

The first declaration of `month` is explicit about returning a `Month` and about
not modifying the state of the `Date` object. The second version leaves the
reader guessing and opens more possibilities for uncaught bugs.

**Example, bad.** This loop is a restricted form of `std::find`:

```cpp
void f(vector<string>& v)
{
    string val;
    cin >> val;
    // ...
    int index = -1;                    // bad, plus should use gsl::index
    for (int i = 0; i < v.size(); ++i) {
        if (v[i] == val) {
            index = i;
            break;
        }
    }
    // ...
}
```

**Example, good.** A much clearer expression of intent would be:

```cpp
void f(vector<string>& v)
{
    string val;
    cin >> val;
    // ...
    auto p = find(begin(v), end(v), val);  // better
    // ...
}
```

A well-designed library expresses intent (what is to be done, rather than just
how something is being done) far better than direct use of language features.

A C++ programmer should know the basics of the standard library, and use it
where appropriate. Any programmer should know the basics of the foundation
libraries of the project being worked on, and use them appropriately. Any
programmer using these guidelines should know the guidelines support library,
and use it appropriately.

**Example.**

```cpp
change_speed(double s);   // bad: what does s signify?
// ...
change_speed(2.3);
```

A better approach is to be explicit about the meaning of the double (new speed
or delta on old speed?) and the unit used:

```cpp
change_speed(Speed s);     // better: the meaning of s is specified
// ...
change_speed(2.3);         // error: no unit
change_speed(23_m / 10s);  // meters per second
```

We could have accepted a plain (unit-less) `double` as a delta, but that would
have been error-prone. If we wanted both absolute speed and deltas, we would
have defined a `Delta` type.

**Enforcement.** Very hard in general.

- use `const` consistently (check if member functions modify their object; check
  if functions modify arguments passed by pointer or reference)
- flag uses of casts (casts neuter the type system)
- detect code that mimics the standard library (hard)

## Rationale

Adopted without argument: it is the premise the other twelve philosophy rules
argue from, and this codebase already leans on it. `Result<T>` exists because
"this can fail" belongs in the signature rather than in a comment — see
[0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md),
which is P.1 applied to error handling.

## Consequences

### Positive

- Gives a written basis for asking that a distinction live in the type system
  rather than in a comment or a naming convention.

### Negative

- Unfalsifiable in review. Two people can disagree about whether a given piece
  of code "expresses its idea directly" with no way to settle it, so the rule
  persuades rather than decides.
- Taken too far it produces a wrapper type per concept, which costs more
  reading than it saves.

### Neutral

- The rule's own enforcement notes admit it is "very hard in general". Nothing
  in this repository checks it.

## Related

- [0008](0008-p-2-write-in-iso-standard-c.md) — the next philosophy rule.
- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  P.1 applied to failure.
