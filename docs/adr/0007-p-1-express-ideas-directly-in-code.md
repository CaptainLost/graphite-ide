# 0007. P.1: Express ideas directly in code

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

The philosophy rules are the ones the concrete rules are derived from. They are
generally not mechanically checkable; individual rules reflecting these themes
are. Without a philosophical basis the more specific, checkable rules lack a
rationale, which is why they are recorded here at all rather than left implicit.

P.1 is the first of them, and it bears on every header: a signature is the whole
contract a caller sees. Compilers do not read comments or
design documents, and neither do many programmers, consistently. What is
expressed in code has defined semantics and can, in principle, be checked by
compilers and other tools. What is expressed only in prose beside the code is
unchecked, and drifts.

The upstream rule shows three shapes of the same failure. A declaration that
leaves the reader guessing:

```cpp
class Date {
public:
    Month month() const;  // do
    int month();          // don't
    // ...
};
```

The first declaration is explicit about returning a `Month` and about not
modifying the state of the `Date` object. The second leaves the reader guessing
and opens more possibilities for uncaught bugs.

A loop that is a restricted form of `std::find`, open-coded:

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

A much clearer expression of intent would be:

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

A well-designed library expresses intent — what is to be done, rather than just
how something is being done — far better than direct use of language features.

And a parameter whose meaning lives nowhere:

```cpp
change_speed(double s);    // bad: what does s signify?
change_speed(2.3);

change_speed(Speed s);     // better: the meaning of s is specified
change_speed(2.3);         // error: no unit
change_speed(23_m / 10s);  // meters per second
```

A plain unit-less `double` could have been accepted as a delta, but that would
have been error-prone. If both absolute speeds and deltas are wanted, a `Delta`
type is defined.

## Decision

Express ideas directly in code. State meaning through the type system and
through named operations, not through comments, naming conventions, or
open-coded mechanics.

Concretely:

- Give a function a return type and a `const` qualification that say what it
  actually does. A query is `const` and returns the type it means; a mutator is
  not `const`.
- Prefer a named standard-library or foundation-library operation to an
  open-coded loop that reimplements one. If a loop is a `find`, a `count`, a
  `copy`, a `transform` or an `any_of`, write that.
- Give a parameter a type that carries its meaning and its unit rather than a
  bare built-in type, wherever the bare type would leave a caller guessing or
  would accept a wrong-but-well-typed argument.

Anyone writing code under this rule is expected to know the basics of the C++
standard library, and of whatever foundation libraries the project at hand uses,
and to reach for them before writing the mechanism by hand.

## Rationale

This is the rule that gives the rest of the philosophy section its teeth. Every
later rule — express intent, prefer compile-time checking, do not leak, do not
waste — is a specialisation of "put it in the code where it can be checked",
and each one is arguable on its own only because this one is settled.

The alternative in practice is not "no rule" but "the rule lives in comments",
which is the state the guideline exists to displace: a comment saying a function
does not modify its argument is worth exactly nothing to the compiler and to the
next reader who does not believe it.

## Consequences

### Positive

- Intent becomes checkable. What a function returns, and whether it mutates its
  object, is derivable from the declaration alone by a reader and by a tool.
- Whole classes of mistake — passing a delta where an absolute value was meant,
  an off-by-one in a hand-written search — become compile-time errors or stop
  existing.
- Code shrinks. A named algorithm is one line where the loop was eight.

### Negative

- Meaning-carrying types have to be defined, named and maintained, and a wrapper
  over a `double` is real code with real weight. There is a threshold below
  which the wrapper costs more than the confusion it prevents, and this record
  does not say where it is.
- Library vocabulary has to be learned before it can be used, and a reader who
  does not know the algorithm reads the one-line version more slowly than the
  loop.
- Rewriting code that mimics the standard library is churn in files that were
  working.

### Neutral

- This rule does not by itself prescribe *which* libraries are the vocabulary;
  that is [0019](0019-p-13-use-support-libraries-as-appropriate.md).
- Very hard to enforce mechanically. It holds by review, and by the more
  specific records that follow it.

## Enforcement

Very hard in general.

- Use `const` consistently: check whether member functions modify their object,
  and whether functions modify arguments passed by pointer or reference.
- Flag uses of casts; casts neuter the type system.
- Detect code that mimics the standard library (hard).

## Related

- [0009](0009-p-3-express-intent.md) — the same idea applied to control flow.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — what the
  type system is being asked to carry.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — the libraries this
  rule expects to be reached for.
